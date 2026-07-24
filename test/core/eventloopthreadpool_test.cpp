#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "currentthread.h"
#include "eventloop.h"
#include "eventloopthread.h"
#include "eventloopthreadpool.h"
#include "nettestutils.h"


using namespace std::chrono_literals;
using namespace NetTestUtils;


// 验证线程数为 0 时，线程池只使用主 loop，并且初始化回调只执行一次。
TEST(EventLoopThreadPoolTest, ZeroThreadsUsesMainLoopAndRunsInitCallbackOnce)
{
    EventLoop mainLoop;
    EventLoopThreadPool pool(&mainLoop, "single");
    std::atomic<int> callbackCount = 0;
    EventLoop *callbackLoop = nullptr;
    pid_t callbackTid = 0;
    bool callbackInLoopThread = false;

    EXPECT_FALSE(pool.started());
    EXPECT_EQ(pool.name(), "single");

    pool.setThreadNum(0);
    // 线程数为 0 时，start() 应该直接把主 loop 交给回调。
    pool.start([&](EventLoop *loop)
               {
                   ++callbackCount;
                   callbackLoop = loop;
                   callbackTid = CurrentThread::tid();
                   callbackInLoopThread = loop->isInLoopThread(); //
               });

    EXPECT_TRUE(pool.started());
    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_EQ(callbackLoop, &mainLoop);
    EXPECT_EQ(callbackTid, CurrentThread::tid());
    EXPECT_TRUE(callbackInLoopThread);

    auto allLoops = pool.getAllLoops();
    ASSERT_EQ(allLoops.size(), 1u);
    EXPECT_EQ(allLoops[0], &mainLoop);

    EXPECT_EQ(pool.getNextLoop(), &mainLoop);
    EXPECT_EQ(pool.getNextLoop(), &mainLoop);
    EXPECT_EQ(pool.getNextLoop(), &mainLoop);
}

// 验证启动后会创建指定数量的 worker loop，并在每个 worker 上执行初始化回调。
TEST(EventLoopThreadPoolTest, StartCreatesRequestedWorkerLoopsAndRunsInitCallbackOnEachWorker)
{
    EventLoop mainLoop;
    EventLoopThreadPool pool(&mainLoop, "workers");
    const pid_t mainTid = CurrentThread::tid();
    std::mutex mutex;
    std::vector<EventLoop *> callbackLoops;
    std::vector<pid_t> callbackTids;
    std::vector<bool> callbackInLoopThread;

    pool.setThreadNum(3);
    // 用 mutex 保护共享 vector，避免多个 worker 同时写入。
    pool.start([&](EventLoop *loop)
               {
                   std::lock_guard<std::mutex> lock(mutex);
                   callbackLoops.push_back(loop);
                   callbackTids.push_back(CurrentThread::tid());
                   callbackInLoopThread.push_back(loop->isInLoopThread()); //
               });

    EXPECT_TRUE(pool.started());

    auto allLoops = pool.getAllLoops();
    ASSERT_EQ(allLoops.size(), 3u);
    EXPECT_EQ(callbackLoops.size(), 3u);
    EXPECT_EQ(callbackTids.size(), 3u);
    EXPECT_EQ(callbackInLoopThread.size(), 3u);

    for (EventLoop *loop : allLoops)
    {
        EXPECT_NE(loop, nullptr);
        EXPECT_NE(loop, &mainLoop);
        EXPECT_FALSE(loop->isInLoopThread());
    }

    EXPECT_EQ(toSet(callbackLoops), toSet(allLoops));
    EXPECT_EQ(toSet(callbackTids).size(), 3u);

    for (pid_t tid : callbackTids) EXPECT_NE(tid, mainTid);

    for (bool inLoopThread : callbackInLoopThread) EXPECT_TRUE(inLoopThread);
}

// 验证 start() 会等待初始化回调结束，再对调用者返回。
TEST(EventLoopThreadPoolTest, StartWaitsForInitCallbackToFinishBeforeReturning)
{
    EventLoop mainLoop;
    EventLoopThreadPool pool(&mainLoop, "blocking");
    std::promise<void> callbackEntered;
    std::promise<void> releaseCallback;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::shared_future<void> allowContinue = releaseCallback.get_future().share();

    pool.setThreadNum(1);

    std::thread releaser([&]()
                         {
                             // 先确认初始化回调确实进来了，再延迟释放它。
                             ASSERT_EQ(callbackEnteredFuture.wait_for(1s), std::future_status::ready);
                             std::this_thread::sleep_for(80ms);
                             releaseCallback.set_value(); //
                         });

    const auto startTime = std::chrono::steady_clock::now();
    pool.start([&](EventLoop *)
               {
                   callbackEntered.set_value();
                   allowContinue.wait(); //
               });
    const auto elapsed = std::chrono::steady_clock::now() - startTime;

    releaser.join();

    EXPECT_GE(elapsed, 70ms);
}

// 验证 getNextLoop() 在子 loop 之间按轮询方式分配。
TEST(EventLoopThreadPoolTest, GetNextLoopUsesRoundRobinAcrossSubLoops)
{
    EventLoop mainLoop;
    EventLoopThreadPool pool(&mainLoop, "rr");

    pool.setThreadNum(3);
    pool.start();

    auto allLoops = pool.getAllLoops();
    ASSERT_EQ(allLoops.size(), 3u);

    EXPECT_EQ(pool.getNextLoop(), allLoops[0]);
    EXPECT_EQ(pool.getNextLoop(), allLoops[1]);
    EXPECT_EQ(pool.getNextLoop(), allLoops[2]);
    EXPECT_EQ(pool.getNextLoop(), allLoops[0]);
    EXPECT_EQ(pool.getNextLoop(), allLoops[1]);
    EXPECT_EQ(pool.getNextLoop(), allLoops[2]);
}

// 验证提交到各 worker loop 的任务分别在对应 worker 线程上执行。
TEST(EventLoopThreadPoolTest, QueuedWorkRunsOnEachWorkerLoopThread)
{
    EventLoop mainLoop;
    EventLoopThreadPool pool(&mainLoop, "dispatch");
    const pid_t mainTid = CurrentThread::tid();

    pool.setThreadNum(3);
    pool.start();

    auto allLoops = pool.getAllLoops();
    ASSERT_EQ(allLoops.size(), 3u);

    std::vector<std::promise<pid_t>> tidPromises(allLoops.size());
    std::vector<std::future<pid_t>> tidFutures;
    std::vector<std::promise<bool>> inLoopPromises(allLoops.size());
    std::vector<std::future<bool>> inLoopFutures;

    tidFutures.reserve(allLoops.size());
    inLoopFutures.reserve(allLoops.size());
    for (size_t i = 0; i < allLoops.size(); ++i)
    {
        tidFutures.emplace_back(tidPromises[i].get_future());
        inLoopFutures.emplace_back(inLoopPromises[i].get_future());

        EventLoop *loop = allLoops[i];
        // 每个 worker loop 都投递一个任务，检查它是否在自己的线程里执行。
        loop->queueInLoop([loop, &tidPromises, &inLoopPromises, i]()
                          {
                              tidPromises[i].set_value(CurrentThread::tid());
                              inLoopPromises[i].set_value(loop->isInLoopThread()); //
                          });
    }

    std::unordered_set<pid_t> workerTids;
    for (size_t i = 0; i < allLoops.size(); ++i)
    {
        ASSERT_EQ(tidFutures[i].wait_for(1s), std::future_status::ready);
        ASSERT_EQ(inLoopFutures[i].wait_for(1s), std::future_status::ready);

        const pid_t workerTid = tidFutures[i].get();
        EXPECT_NE(workerTid, mainTid);
        EXPECT_TRUE(inLoopFutures[i].get());
        workerTids.insert(workerTid);
    }

    EXPECT_EQ(workerTids.size(), allLoops.size());
}

// 验证 getNextLoop() 选出的 loop 集合与 getAllLoops() 暴露的一致。
TEST(EventLoopThreadPoolTest, GetNextLoopReturnsSameWorkersExposedByGetAllLoops)
{
    EventLoop mainLoop;
    EventLoopThreadPool pool(&mainLoop, "stable");

    pool.setThreadNum(2);
    pool.start();

    auto allLoops = pool.getAllLoops();
    ASSERT_EQ(allLoops.size(), 2u);

    std::vector<EventLoop *> selected;
    for (size_t i = 0; i < allLoops.size() * 3; ++i) selected.push_back(pool.getNextLoop());

    EXPECT_EQ(toSet(selected), toSet(allLoops));
}

// 验证析构线程池时会等待 worker 上正在运行的回调结束。
TEST(EventLoopThreadPoolTest, DestructorWaitsForRunningWorkerCallbackToFinish)
{
    EventLoop mainLoop;
    auto pool = std::make_unique<EventLoopThreadPool>(&mainLoop, "teardown");
    std::promise<void> callbackStarted;
    std::promise<void> releaseCallback;
    std::promise<void> callbackFinished;
    auto callbackStartedFuture = callbackStarted.get_future();
    auto callbackFinishedFuture = callbackFinished.get_future();
    std::shared_future<void> allowFinish = releaseCallback.get_future().share();

    pool->setThreadNum(2);
    pool->start();

    auto allLoops = pool->getAllLoops();
    ASSERT_EQ(allLoops.size(), 2u);

    allLoops[0]->queueInLoop([&]()
                             {
                                 // 让析构卡在一个未结束的 worker 回调里。
                                 callbackStarted.set_value();
                                 allowFinish.wait();
                                 callbackFinished.set_value(); //
                             });

    ASSERT_EQ(callbackStartedFuture.wait_for(1s), std::future_status::ready);

    auto destroyFuture = std::async(std::launch::async, [&pool]()
                                    { pool.reset(); });

    EXPECT_EQ(destroyFuture.wait_for(50ms), std::future_status::timeout);

    releaseCallback.set_value();

    EXPECT_EQ(callbackFinishedFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(destroyFuture.wait_for(1s), std::future_status::ready);
}

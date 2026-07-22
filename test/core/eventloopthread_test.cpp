#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "currentthread.h"
#include "eventloop.h"
#include "eventloopthread.h"


using namespace std::chrono_literals;


// 验证 startLoop() 返回的 EventLoop 可用，且实际运行在工作线程上。
TEST(EventLoopThreadTest, StartLoopReturnsUsableLoopRunningOnWorkerThread)
{
    const pid_t mainTid = CurrentThread::tid();
    std::promise<pid_t> callbackTid;
    std::promise<bool> callbackInLoopThread;
    auto callbackTidFuture = callbackTid.get_future();
    auto callbackInLoopThreadFuture = callbackInLoopThread.get_future();

    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    ASSERT_NE(loop, nullptr);
    EXPECT_FALSE(loop->isInLoopThread());

    // 把校验放进 loop 回调里，确认它确实跑在 worker 线程。
    loop->queueInLoop([&]()
                      {
                          callbackTid.set_value(CurrentThread::tid());
                          callbackInLoopThread.set_value(loop->isInLoopThread()); //
                      });

    EXPECT_EQ(callbackTidFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(callbackInLoopThreadFuture.wait_for(1s), std::future_status::ready);

    EXPECT_NE(callbackTidFuture.get(), mainTid);
    EXPECT_TRUE(callbackInLoopThreadFuture.get());
}

// 验证初始化回调只执行一次，并且拿到的就是 startLoop 返回的那个 loop。
TEST(EventLoopThreadTest, InitCallbackRunsOnceOnWorkerThreadAndSeesReturnedLoop)
{
    const pid_t mainTid = CurrentThread::tid();
    std::atomic<int> callbackCount = 0;
    std::promise<EventLoop *> callbackLoop;
    std::promise<pid_t> callbackTid;
    std::promise<bool> callbackInLoopThread;
    auto callbackLoopFuture = callbackLoop.get_future();
    auto callbackTidFuture = callbackTid.get_future();
    auto callbackInLoopThreadFuture = callbackInLoopThread.get_future();

    EventLoopThread loopThread([&](EventLoop *loop)
                               {
                                   ++callbackCount;
                                   callbackLoop.set_value(loop);
                                   callbackTid.set_value(CurrentThread::tid());
                                   callbackInLoopThread.set_value(loop->isInLoopThread()); });

    EventLoop *returnedLoop = loopThread.startLoop();

    ASSERT_NE(returnedLoop, nullptr);
    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_EQ(callbackLoopFuture.get(), returnedLoop);
    EXPECT_NE(callbackTidFuture.get(), mainTid);
    EXPECT_TRUE(callbackInLoopThreadFuture.get());
}

// 验证 startLoop() 会等待初始化回调结束后才返回。
TEST(EventLoopThreadTest, StartLoopWaitsForInitCallbackToFinish)
{
    std::promise<void> callbackEntered;
    std::promise<void> releaseCallback;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::shared_future<void> allowContinue = releaseCallback.get_future().share();

    // 用外层 async 调 startLoop()，观察它是否会被初始化回调阻塞住。
    EventLoopThread loopThread([&](EventLoop *)
                               {
                                   callbackEntered.set_value();
                                   allowContinue.wait(); });

    auto started = std::async(std::launch::async, [&loopThread]()
                              { return loopThread.startLoop(); });

    ASSERT_EQ(callbackEnteredFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(started.wait_for(50ms), std::future_status::timeout);

    releaseCallback.set_value();

    ASSERT_EQ(started.wait_for(1s), std::future_status::ready);
    EventLoop *loop = started.get();
    ASSERT_NE(loop, nullptr);

    loop->quit();
}

// 验证初始化回调中调用 runInLoop 会立即在该工作线程内执行。
TEST(EventLoopThreadTest, InitCallbackCanRunWorkImmediatelyInLoopThread)
{
    std::vector<int> order;
    std::promise<pid_t> runInLoopTid;
    auto runInLoopTidFuture = runInLoopTid.get_future();

    // 初始化回调里直接投递任务，验证它会在同一 worker 线程内立即执行。
    EventLoopThread loopThread([&](EventLoop *loop)
                               {
                                   order.push_back(1);
                                   loop->runInLoop([&]()
                                                   {
                                                       order.push_back(2);
                                                       runInLoopTid.set_value(CurrentThread::tid());
                                                   });
                                   order.push_back(3); });

    EventLoop *loop = loopThread.startLoop();

    ASSERT_NE(loop, nullptr);
    EXPECT_EQ(runInLoopTidFuture.wait_for(1s), std::future_status::ready);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
    EXPECT_NE(runInLoopTidFuture.get(), CurrentThread::tid());
}

// 验证从外部线程 runInLoop，任务最终会落到线程拥有的 loop 上执行。
TEST(EventLoopThreadTest, RunInLoopFromCallerThreadExecutesOnOwnedLoopThread)
{
    std::promise<pid_t> initTid;
    std::promise<pid_t> runInLoopTid;
    std::promise<void> callbackDone;
    auto initTidFuture = initTid.get_future();
    auto runInLoopTidFuture = runInLoopTid.get_future();
    auto callbackDoneFuture = callbackDone.get_future();

    EventLoopThread loopThread([&](EventLoop *)
                               { initTid.set_value(CurrentThread::tid()); });

    EventLoop *loop = loopThread.startLoop();

    ASSERT_NE(loop, nullptr);

    // 从外部线程发起 runInLoop，让它回到 owner loop 执行。
    loop->runInLoop([&]()
                    {
                        runInLoopTid.set_value(CurrentThread::tid());
                        callbackDone.set_value(); //
                    });

    ASSERT_EQ(callbackDoneFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(runInLoopTidFuture.get(), initTidFuture.get());
}

// 验证析构时会先退出 loop，并等待正在运行的回调收尾。
TEST(EventLoopThreadTest, DestructorQuitsLoopAndWaitsForRunningCallback)
{
    std::promise<void> callbackStarted;
    std::promise<void> releaseCallback;
    std::promise<void> callbackFinished;
    auto callbackStartedFuture = callbackStarted.get_future();
    auto callbackFinishedFuture = callbackFinished.get_future();
    std::shared_future<void> allowFinish = releaseCallback.get_future().share();

    auto loopThread = std::make_unique<EventLoopThread>();
    EventLoop *loop = loopThread->startLoop();

    ASSERT_NE(loop, nullptr);

    // 让析构发生在 callback 运行中，观察它是否会等待回调退出。
    loop->queueInLoop([&]()
                      {
                          callbackStarted.set_value();
                          allowFinish.wait();
                          callbackFinished.set_value(); //
                      });

    ASSERT_EQ(callbackStartedFuture.wait_for(1s), std::future_status::ready);

    auto destroyFuture = std::async(std::launch::async, [&loopThread]()
                                    { loopThread.reset(); });

    EXPECT_EQ(destroyFuture.wait_for(50ms), std::future_status::timeout);

    releaseCallback.set_value();

    EXPECT_EQ(callbackFinishedFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(destroyFuture.wait_for(1s), std::future_status::ready);
}

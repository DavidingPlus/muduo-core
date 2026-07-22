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

TEST(EventLoopThreadTest, StartLoopWaitsForInitCallbackToFinish)
{
    std::promise<void> callbackEntered;
    std::promise<void> releaseCallback;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::shared_future<void> allowContinue = releaseCallback.get_future().share();

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

TEST(EventLoopThreadTest, InitCallbackCanRunWorkImmediatelyInLoopThread)
{
    std::vector<int> order;
    std::promise<pid_t> runInLoopTid;
    auto runInLoopTidFuture = runInLoopTid.get_future();

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

    loop->runInLoop([&]()
                    {
                        runInLoopTid.set_value(CurrentThread::tid());
                        callbackDone.set_value(); //
                    });

    ASSERT_EQ(callbackDoneFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(runInLoopTidFuture.get(), initTidFuture.get());
}

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

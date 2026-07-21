#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "currentthread.h"
#include "eventloop.h"


using namespace std::chrono_literals;


TEST(EventLoopTest, RunInLoopExecutesImmediatelyInOwnerThread)
{
    EventLoop loop;
    bool called = false;
    pid_t callbackTid = 0;

    loop.runInLoop([&]()
                   {
                       called = true;
                       callbackTid = CurrentThread::tid(); //
                   });

    EXPECT_TRUE(called);
    EXPECT_EQ(callbackTid, CurrentThread::tid());
}

TEST(EventLoopTest, RunInLoopIsSynchronousWhenCalledFromLoopThread)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;
    std::vector<int> order;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               ready.set_value(&loop);
                               loop.loop();
                               exited.set_value(); //
                           });

    EventLoop *loop = ready.get_future().get();

    loop->queueInLoop([&]()
                      {
                          order.push_back(1);
                          loop->runInLoop([&]()
                                          { order.push_back(2); });
                          order.push_back(3);
                          loop->quit(); //
                      });

    EXPECT_EQ(exited.get_future().wait_for(1s), std::future_status::ready);
    loopThread.join();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(EventLoopTest, QueueInLoopFromOtherThreadExecutesOnLoopThread)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;
    std::promise<pid_t> ownerTid;
    std::promise<pid_t> callbackTid;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               ownerTid.set_value(CurrentThread::tid());
                               ready.set_value(&loop);
                               loop.loop();
                               exited.set_value(); //
                           });

    EventLoop *loop = ready.get_future().get();

    loop->queueInLoop([&]()
                      {
                          callbackTid.set_value(CurrentThread::tid());
                          loop->quit(); //
                      });

    EXPECT_EQ(exited.get_future().wait_for(1s), std::future_status::ready);
    loopThread.join();

    EXPECT_EQ(callbackTid.get_future().get(), ownerTid.get_future().get());
}

TEST(EventLoopTest, QueueInLoopDuringPendingFunctorTriggersNextIteration)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;
    std::promise<void> innerExecuted;
    std::vector<int> order;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               ready.set_value(&loop);
                               loop.loop();
                               exited.set_value(); //
                           });

    EventLoop *loop = ready.get_future().get();

    loop->queueInLoop([&]()
                      {
                          order.push_back(1);
                          loop->queueInLoop([&]()
                                            {
                                                order.push_back(3);
                                                innerExecuted.set_value();
                                                loop->quit();//
                                            });
                          order.push_back(2); });

    const auto status = innerExecuted.get_future().wait_for(1s);
    if (status != std::future_status::ready)
    {
        loop->quit();
    }

    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_EQ(exited.get_future().wait_for(1s), std::future_status::ready);
    loopThread.join();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(EventLoopTest, QuitFromOtherThreadWakesBlockedLoopPromptly)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               ready.set_value(&loop);
                               loop.loop();
                               exited.set_value(); //
                           });

    EventLoop *loop = ready.get_future().get();

    std::this_thread::sleep_for(50ms);
    loop->quit();

    EXPECT_EQ(exited.get_future().wait_for(1s), std::future_status::ready);
    loopThread.join();
}

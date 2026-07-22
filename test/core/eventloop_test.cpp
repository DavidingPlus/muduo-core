#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "currentthread.h"
#include "eventloop.h"


using namespace std::chrono_literals;


// 验证在所属线程里调用 runInLoop 会直接同步执行。
TEST(EventLoopTest, RunInLoopExecutesImmediatelyInOwnerThread)
{
    EventLoop loop;
    bool called = false;
    pid_t callbackTid = 0;

    // 直接在 owner thread 调用 runInLoop，回调应当立即执行。
    loop.runInLoop([&]()
                   {
                       called = true;
                       callbackTid = CurrentThread::tid(); //
                   });

    EXPECT_TRUE(called);
    EXPECT_EQ(callbackTid, CurrentThread::tid());
}

// 验证 loop 线程内部再次 runInLoop 不会被延后到下一轮。
TEST(EventLoopTest, RunInLoopIsSynchronousWhenCalledFromLoopThread)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;
    std::vector<int> order;

    // 先把 loop 线程跑起来，再从回调里嵌套 runInLoop 验证同步语义。
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

// 验证从其他线程 queueInLoop，回调最终会在 loop 所属线程执行。
TEST(EventLoopTest, QueueInLoopFromOtherThreadExecutesOnLoopThread)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;
    std::promise<pid_t> ownerTid;
    std::promise<pid_t> callbackTid;

    // ownerTid 用来对照 callback 实际运行的线程，确认它没跑偏。
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

// 验证在执行中的回调里再次 queueInLoop，会被推迟到下一轮循环处理。
TEST(EventLoopTest, QueueInLoopDuringPendingFunctorTriggersNextIteration)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;
    std::promise<void> innerExecuted;
    std::vector<int> order;

    // 这里用二次 queueInLoop 模拟“执行中再投递任务”的场景。
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
        // 如果内层任务迟迟没跑完，主动退出避免测试挂死。
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

// 验证其他线程调用 quit() 可以及时唤醒阻塞中的 loop。
TEST(EventLoopTest, QuitFromOtherThreadWakesBlockedLoopPromptly)
{
    std::promise<EventLoop *> ready;
    std::promise<void> exited;

    // 先阻塞在 loop()，再从其他线程调用 quit() 把它唤醒。
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

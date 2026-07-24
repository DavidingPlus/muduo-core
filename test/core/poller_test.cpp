#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <sys/eventfd.h>
#include <unistd.h>

#include "channel.h"
#include "epollpoller.h"
#include "eventloop.h"
#include "nettestutils.h"
#include "poller.h"


using namespace std::chrono_literals;
using namespace NetTestUtils;


// 验证 Poller 通过内部 fd 映射判断某个 Channel 是否已注册。
TEST(PollerTest, HasChannelUsesStoredDescriptorMapping)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel tracked(&loop, fd.get());
    Channel other(&loop, fd.get());
    StubPoller poller(&loop);

    EXPECT_FALSE(poller.hasChannel(&tracked));

    poller.track(&tracked);

    EXPECT_TRUE(poller.hasChannel(&tracked));
    EXPECT_FALSE(poller.hasChannel(&other));
}

// 验证默认 Poller 工厂会读取环境变量并按配置返回实现。
TEST(PollerTest, DefaultFactoryHonorsConfiguredImplementation)
{
    ScopedEnvVar env("MUDUO_DEFAULT_POLLER");

    // 先清空环境变量，确认默认分支会回落到 EPollPoller。
    ::unsetenv("MUDUO_DEFAULT_POLLER");
    {
        std::unique_ptr<Poller> poller(Poller::NewDefaultPoller(nullptr));
        ASSERT_NE(poller, nullptr);
        EXPECT_NE(dynamic_cast<EPollPoller *>(poller.get()), nullptr);
    }

    // 再切到不支持的实现名，确认工厂会拒绝创建。
    ::setenv("MUDUO_DEFAULT_POLLER", "Poll", 1);
    EXPECT_EQ(Poller::NewDefaultPoller(nullptr), nullptr);
}

// 验证 epoll 事件能被投递到 Channel 回调，同时 EventLoop 会记录轮询返回时间。
TEST(PollerTest, EpollBackedLoopDispatchesReadableChannelAndRecordsPollTime)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    std::atomic<int> readCount = 0;

    // 回调里先消费 eventfd，再主动退出 loop，方便验证只触发一次。
    channel.setReadCallback([&](const Timestamp &)
                            {
                                ++readCount;
                                readEventFd(fd.get());
                                loop.quit(); //
                            });
    channel.enableReading();

    std::thread writer([&]()
                       {
                           std::this_thread::sleep_for(20ms);
                           writeEventFd(fd.get()); //
                       });

    loop.loop();
    writer.join();

    EXPECT_EQ(readCount.load(), 1);
    EXPECT_TRUE(loop.hasChannel(&channel));
    EXPECT_EQ(channel.index(), Channel::kAdded);
    EXPECT_GT(loop.pollReturnTime().microSecondsSinceEpoch(), 0);

    channel.disableAll();
    channel.remove();
}

// 验证 disableAll() 后，内核不再继续上报该 fd 的可读事件。
TEST(PollerTest, DisableAllStopsKernelFromReportingReadableEvents)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    std::atomic<int> readCount = 0;

    // 这里先注册可读事件，再立即 disableAll()，测试内核不会再上报它。
    channel.setReadCallback([&](const Timestamp &)
                            {
                                ++readCount;
                                readEventFd(fd.get()); //
                            });
    channel.enableReading();
    channel.disableAll();

    EXPECT_EQ(channel.index(), Channel::kDeleted);
    EXPECT_TRUE(loop.hasChannel(&channel));

    std::thread writer([&]()
                       {
                           std::this_thread::sleep_for(20ms);
                           writeEventFd(fd.get()); //
                       });
    std::thread quitter([&]()
                        {
                            std::this_thread::sleep_for(80ms);
                            loop.quit(); //
                        });

    loop.loop();
    writer.join();
    quitter.join();

    EXPECT_EQ(readCount.load(), 0);

    channel.remove();
    EXPECT_FALSE(loop.hasChannel(&channel));
}

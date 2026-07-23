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
#include "poller.h"


using namespace std::chrono_literals;


namespace
{

    // RAII 包装 eventfd，避免测试提前结束时泄漏 fd。
    class ScopedFd
    {

    public:

        explicit ScopedFd(int fd) : m_fd(fd) {}

        ~ScopedFd()
        {
            if (m_fd >= 0) ::close(m_fd);
        }

        int get() const { return m_fd; }


    private:

        int m_fd = -1;
    };

    // 临时覆盖环境变量，结束时恢复现场，避免污染其他用例。
    class ScopedEnvVar
    {

    public:

        explicit ScopedEnvVar(const char *name) : m_name(name)
        {
            const char *value = ::getenv(m_name.c_str());
            if (value)
            {
                m_hadValue = true;
                m_value = value;
            }
        }

        ~ScopedEnvVar()
        {
            if (m_hadValue)
            {
                ::setenv(m_name.c_str(), m_value.c_str(), 1);
            }
            else
            {
                ::unsetenv(m_name.c_str());
            }
        }


    private:

        std::string m_name;
        std::string m_value;
        bool m_hadValue = false;
    };

    // 用于验证 Poller 接口的最小 stub 实现。
    class StubPoller : public Poller
    {

    public:

        explicit StubPoller(EventLoop *loop) : Poller(loop) {}

        Timestamp poll(int, ChannelList *) override;

        void updateChannel(Channel *) override {}

        void removeChannel(Channel *) override {}

        void track(Channel *channel) { m_channels[channel->fd()] = channel; }
    };

    Timestamp StubPoller::poll(int, ChannelList *)
    {
        return Timestamp::Now();
    }

    // 生成一个非阻塞、close-on-exec 的 eventfd 作为测试源。
    int createEventFd()
    {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        EXPECT_GE(fd, 0);
        return fd;
    }

    // 往 eventfd 写入一次，制造可读事件。
    void writeEventFd(int fd, uint64_t value = 1)
    {
        ASSERT_EQ(::write(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    }

    // 把 eventfd 里的计数读空，避免后续测试误判为还有可读事件。
    void readEventFd(int fd)
    {
        uint64_t value = 0;
        ASSERT_EQ(::read(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    }

} // namespace


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

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
        return Timestamp::now();
    }

    int createEventFd()
    {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        EXPECT_GE(fd, 0);
        return fd;
    }

    void writeEventFd(int fd, uint64_t value = 1)
    {
        ASSERT_EQ(::write(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    }

    void readEventFd(int fd)
    {
        uint64_t value = 0;
        ASSERT_EQ(::read(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    }

} // namespace


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

TEST(PollerTest, DefaultFactoryHonorsConfiguredImplementation)
{
    ScopedEnvVar env("MUDUO_DEFAULT_POLLER");

    ::unsetenv("MUDUO_DEFAULT_POLLER");
    {
        std::unique_ptr<Poller> poller(Poller::newDefaultPoller(nullptr));
        ASSERT_NE(poller, nullptr);
        EXPECT_NE(dynamic_cast<EPollPoller *>(poller.get()), nullptr);
    }

    ::setenv("MUDUO_DEFAULT_POLLER", "Poll", 1);
    EXPECT_EQ(Poller::newDefaultPoller(nullptr), nullptr);
}

TEST(PollerTest, EpollBackedLoopDispatchesReadableChannelAndRecordsPollTime)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    std::atomic<int> readCount = 0;

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

TEST(PollerTest, DisableAllStopsKernelFromReportingReadableEvents)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    std::atomic<int> readCount = 0;

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

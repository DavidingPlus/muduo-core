#include <gtest/gtest.h>

#include <atomic>
#include <memory>

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "channel.h"
#include "eventloop.h"
#include "timestamp.h"


namespace
{

    // RAII 包装 eventfd，避免 Channel 用例提前返回时泄漏 fd。
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

    // 生成可用于 epoll 事件测试的 eventfd。
    int createEventFd()
    {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        EXPECT_GE(fd, 0);
        return fd;
    }

} // namespace


// 验证 enable/disable/remove 这些状态切换会同步反映到 Channel 和 EventLoop 的注册关系。
TEST(ChannelTest, InterestStateTransitionsTrackLoopRegistration)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());

    EXPECT_TRUE(channel.isNoneEvent());
    EXPECT_FALSE(channel.isReading());
    EXPECT_FALSE(channel.isWriting());
    EXPECT_EQ(channel.index(), Channel::kNew);
    EXPECT_FALSE(loop.hasChannel(&channel));

    channel.enableReading();

    EXPECT_FALSE(channel.isNoneEvent());
    EXPECT_TRUE(channel.isReading());
    EXPECT_FALSE(channel.isWriting());
    EXPECT_EQ(channel.index(), Channel::kAdded);
    EXPECT_TRUE(loop.hasChannel(&channel));

    channel.enableWriting();

    EXPECT_TRUE(channel.isReading());
    EXPECT_TRUE(channel.isWriting());
    EXPECT_EQ(channel.index(), Channel::kAdded);

    channel.disableReading();

    EXPECT_FALSE(channel.isReading());
    EXPECT_TRUE(channel.isWriting());
    EXPECT_FALSE(channel.isNoneEvent());

    channel.disableAll();

    EXPECT_TRUE(channel.isNoneEvent());
    EXPECT_EQ(channel.index(), Channel::kDeleted);
    EXPECT_TRUE(loop.hasChannel(&channel));

    channel.enableReading();

    EXPECT_TRUE(channel.isReading());
    EXPECT_EQ(channel.index(), Channel::kAdded);
    EXPECT_TRUE(loop.hasChannel(&channel));

    channel.remove();

    EXPECT_EQ(channel.index(), Channel::kNew);
    EXPECT_FALSE(loop.hasChannel(&channel));
}

// 验证不同 revents 会触发对应的 read/write/close/error 回调。
TEST(ChannelTest, HandleEventDispatchesCallbacksByReadyFlags)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    const Timestamp receiveTime(123456789);
    const Timestamp readableHupTime(987654321);

    int readCount = 0;
    int writeCount = 0;
    int closeCount = 0;
    int errorCount = 0;
    int64_t lastReadTime = 0;

    channel.setReadCallback([&](const Timestamp &time)
                            {
                                ++readCount;
                                lastReadTime = time.microSecondsSinceEpoch(); });
    channel.setWriteCallback([&]()
                             { ++writeCount; });
    channel.setCloseCallback([&]()
                             { ++closeCount; });
    channel.setErrorCallback([&]()
                             { ++errorCount; });

    channel.setRevents(EPOLLIN | EPOLLOUT | EPOLLERR);
    channel.handleEvent(receiveTime);

    EXPECT_EQ(readCount, 1);
    EXPECT_EQ(writeCount, 1);
    EXPECT_EQ(closeCount, 0);
    EXPECT_EQ(errorCount, 1);
    EXPECT_EQ(lastReadTime, receiveTime.microSecondsSinceEpoch());

    channel.setRevents(EPOLLHUP | EPOLLIN);
    channel.handleEvent(readableHupTime);

    EXPECT_EQ(readCount, 2);
    EXPECT_EQ(closeCount, 0);
    EXPECT_EQ(lastReadTime, readableHupTime.microSecondsSinceEpoch());

    channel.setRevents(EPOLLHUP);
    channel.handleEvent(receiveTime);

    EXPECT_EQ(closeCount, 1);
}

// 验证 tie 绑定的 owner 已失效时，事件回调会被抑制。
TEST(ChannelTest, TieSuppressesCallbacksAfterOwnerExpires)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    std::atomic<int> callbackCount = 0;

    channel.setReadCallback([&](const Timestamp &)
                            { ++callbackCount; });
    channel.setWriteCallback([&]()
                             { ++callbackCount; });
    channel.setCloseCallback([&]()
                             { ++callbackCount; });
    channel.setErrorCallback([&]()
                             { ++callbackCount; });

    auto owner = std::make_shared<int>(7);
    channel.tie(owner);
    owner.reset();

    channel.setRevents(EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP);
    channel.handleEvent(Timestamp::Now());

    EXPECT_EQ(callbackCount.load(), 0);
}

// 验证 owner 仍然存活时，tie 不会阻止事件回调执行。
TEST(ChannelTest, TieKeepsCallbacksActiveWhileOwnerExists)
{
    EventLoop loop;
    ScopedFd fd(createEventFd());
    Channel channel(&loop, fd.get());
    std::atomic<int> readCount = 0;
    auto owner = std::make_shared<int>(42);

    channel.tie(owner);
    channel.setReadCallback([&](const Timestamp &)
                            { ++readCount; });

    channel.setRevents(EPOLLPRI);
    channel.handleEvent(Timestamp::Now());

    EXPECT_EQ(readCount.load(), 1);
}

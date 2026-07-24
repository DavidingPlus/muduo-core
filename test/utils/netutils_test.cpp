#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "eventloop.h"
#include "netutils.h"


// 验证 CreateEventfd() 创建出的 eventfd 具备非阻塞/CLOEXEC 属性，并且初始计数器为 0。
TEST(NetUtilsTest, CreateEventfdSetsExpectedFlagsAndStartsEmpty)
{
    const int fd = NetUtils::CreateEventfd();
    ASSERT_GE(fd, 0);

    const int fdFlags = ::fcntl(fd, F_GETFD);
    const int statusFlags = ::fcntl(fd, F_GETFL);
    ASSERT_GE(fdFlags, 0);
    ASSERT_GE(statusFlags, 0);

    EXPECT_TRUE((fdFlags & FD_CLOEXEC) != 0);
    EXPECT_TRUE((statusFlags & O_NONBLOCK) != 0);

    uint64_t value = 0;
    EXPECT_EQ(::read(fd, &value, sizeof(value)), -1);
    EXPECT_EQ(errno, EAGAIN);

    const uint64_t one = 1;
    ASSERT_EQ(::write(fd, &one, sizeof(one)), static_cast<ssize_t>(sizeof(one)));
    ASSERT_EQ(::read(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    EXPECT_EQ(value, 1u);

    ::close(fd);
}

// 验证 CreateSocketNonblocking() 返回的是 TCP stream socket，并配置了 NONBLOCK/CLOEXEC。
TEST(NetUtilsTest, CreateSocketNonblockingReturnsTcpSocketWithExpectedFlags)
{
    const int fd = NetUtils::CreateSocketNonblocking();
    ASSERT_GE(fd, 0);

    const int fdFlags = ::fcntl(fd, F_GETFD);
    const int statusFlags = ::fcntl(fd, F_GETFL);
    ASSERT_GE(fdFlags, 0);
    ASSERT_GE(statusFlags, 0);

    int type = 0;
    socklen_t typeLen = sizeof(type);
    ASSERT_EQ(::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &typeLen), 0);

    EXPECT_TRUE((fdFlags & FD_CLOEXEC) != 0);
    EXPECT_TRUE((statusFlags & O_NONBLOCK) != 0);
    EXPECT_EQ(type, SOCK_STREAM);

    ::close(fd);
}

// 验证 CheckLoopNotNull() 会原样返回有效指针。
TEST(NetUtilsTest, CheckLoopNotNullReturnsInputLoop)
{
    EventLoop loop;
    EXPECT_EQ(NetUtils::CheckLoopNotNull(&loop), &loop);
}

// 验证 CheckLoopNotNull() 在收到空指针时会按约定终止进程。
TEST(NetUtilsTest, CheckLoopNotNullDiesOnNullptr)
{
    ASSERT_DEATH({ (void)NetUtils::CheckLoopNotNull(nullptr); }, ".*");
}

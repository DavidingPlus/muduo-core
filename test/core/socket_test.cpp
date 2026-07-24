#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "socket.h"
#include "inetaddress.h"
#include "nettestutils.h"

using namespace NetTestUtils;


// 验证 Socket 析构时会关闭它拥有的 fd。
TEST(SocketTest, DestructorClosesOwnedFileDescriptor)
{
    const int fd = createTcpSocket();
    ASSERT_GE(fd, 0);

    {
        Socket socket(fd);
        EXPECT_EQ(socket.fd(), fd);
    }

    errno = 0;
    EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

// 验证各类 socket option setter 真正修改了内核选项。
TEST(SocketTest, SettersUpdateSocketOptions)
{
    const int fd = createTcpSocket();
    ASSERT_GE(fd, 0);

    Socket socket(fd);

    socket.setTcpNoDelay(true);
    EXPECT_EQ(getSocketOption(fd, IPPROTO_TCP, TCP_NODELAY), 1);
    socket.setTcpNoDelay(false);
    EXPECT_EQ(getSocketOption(fd, IPPROTO_TCP, TCP_NODELAY), 0);

    socket.setReuseAddr(true);
    EXPECT_EQ(getSocketOption(fd, SOL_SOCKET, SO_REUSEADDR), 1);
    socket.setReuseAddr(false);
    EXPECT_EQ(getSocketOption(fd, SOL_SOCKET, SO_REUSEADDR), 0);

    socket.setReusePort(true);
    EXPECT_EQ(getSocketOption(fd, SOL_SOCKET, SO_REUSEPORT), 1);
    socket.setReusePort(false);
    EXPECT_EQ(getSocketOption(fd, SOL_SOCKET, SO_REUSEPORT), 0);

    socket.setKeepAlive(true);
    EXPECT_EQ(getSocketOption(fd, SOL_SOCKET, SO_KEEPALIVE), 1);
    socket.setKeepAlive(false);
    EXPECT_EQ(getSocketOption(fd, SOL_SOCKET, SO_KEEPALIVE), 0);
}

// 验证 bind/listen/accept 能正确拿到对端地址，并设置非阻塞和 close-on-exec 标志。
TEST(SocketTest, BindListenAndAcceptPopulatePeerAddressAndFlags)
{
    const int listenFd = createTcpSocket();
    ASSERT_GE(listenFd, 0);

    Socket listenSocket(listenFd);
    listenSocket.setReuseAddr(true);

    const InetAddress localAddress(0, "127.0.0.1");
    listenSocket.bindAddress(localAddress);
    listenSocket.listen();

    const uint16_t port = getBoundPort(listenFd);
    ASSERT_NE(port, 0);

    const int clientFd = createTcpSocket();
    ASSERT_GE(clientFd, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = ::htons(port);
    ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr), 1);
    ASSERT_EQ(::connect(clientFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)), 0);

    InetAddress peerAddress;
    const int connFd = listenSocket.accept(&peerAddress);
    ASSERT_GE(connFd, 0);

    EXPECT_EQ(peerAddress.toIp(), "127.0.0.1");
    EXPECT_NE(peerAddress.toPort(), 0);

    const int statusFlags = ::fcntl(connFd, F_GETFL);
    ASSERT_NE(statusFlags, -1);
    EXPECT_NE(statusFlags & O_NONBLOCK, 0);

    const int descriptorFlags = ::fcntl(connFd, F_GETFD);
    ASSERT_NE(descriptorFlags, -1);
    EXPECT_NE(descriptorFlags & FD_CLOEXEC, 0);

    ::close(connFd);
    ::close(clientFd);
}

// 验证 shutdownWrite() 只关闭写半边，客户端随后会读到 EOF。
TEST(SocketTest, ShutdownWriteHalfClosesConnection)
{
    const int listenFd = createTcpSocket();
    ASSERT_GE(listenFd, 0);

    Socket listenSocket(listenFd);
    listenSocket.setReuseAddr(true);
    listenSocket.bindAddress(InetAddress(0, "127.0.0.1"));
    listenSocket.listen();

    const uint16_t port = getBoundPort(listenFd);
    ASSERT_NE(port, 0);

    const int clientFd = createTcpSocket();
    ASSERT_GE(clientFd, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = ::htons(port);
    ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr), 1);
    ASSERT_EQ(::connect(clientFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)), 0);

    InetAddress peerAddress;
    const int connFd = listenSocket.accept(&peerAddress);
    ASSERT_GE(connFd, 0);

    {
        Socket connection(connFd);
        connection.shutdownWrite();
    }

    char byte = '\0';
    EXPECT_EQ(::recv(clientFd, &byte, sizeof(byte), 0), 0);

    ::close(clientFd);
}

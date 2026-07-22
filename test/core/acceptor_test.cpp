#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "acceptor.h"
#include "currentthread.h"
#include "eventloop.h"
#include "inetaddress.h"


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

        int release()
        {
            int fd = m_fd;
            m_fd = -1;
            return fd;
        }


    private:

        int m_fd = -1;
    };

    struct ListeningSocketInfo
    {
        int fd = -1;
        std::string ip;
        uint16_t port = 0;
        bool reuseAddr = false;
        bool reusePort = false;
        bool nonblocking = false;
        bool cloexec = false;
    };

    struct AcceptedConnectionInfo
    {
        int fd = -1;
        std::string ip;
        uint16_t port = 0;
        pid_t tid = 0;
        bool nonblocking = false;
        bool cloexec = false;
    };

    int createTcpSocket()
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);
        return fd;
    }

    void connectToPort(int fd, uint16_t port)
    {
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = ::htons(port);
        ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr), 1);
        ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)), 0);
    }

    std::optional<ListeningSocketInfo> findListeningSocketInfo()
    {
        DIR *dir = ::opendir("/proc/self/fd");
        if (!dir) return std::nullopt;

        std::optional<ListeningSocketInfo> result;
        while (dirent *entry = ::readdir(dir))
        {
            if (0 == std::strcmp(entry->d_name, ".") || 0 == std::strcmp(entry->d_name, "..")) continue;

            char *end = nullptr;
            const long parsed = std::strtol(entry->d_name, &end, 10);
            if (!end || *end != '\0' || parsed < 0) continue;

            const int fd = static_cast<int>(parsed);
            int acceptConn = 0;
            socklen_t optLen = sizeof(acceptConn);
            if (::getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &acceptConn, &optLen) != 0 || !acceptConn) continue;

            sockaddr_in addr{};
            socklen_t addrLen = sizeof(addr);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen) != 0) continue;
            if (addr.sin_family != AF_INET) continue;
            if (0 != std::strcmp(::inet_ntoa(addr.sin_addr), "127.0.0.1")) continue;

            int reuseAddr = 0;
            int reusePort = 0;
            int fdFlags = ::fcntl(fd, F_GETFD);
            int statusFlags = ::fcntl(fd, F_GETFL);
            if (fdFlags < 0 || statusFlags < 0) continue;

            socklen_t boolLen = sizeof(reuseAddr);
            if (::getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, &boolLen) != 0) continue;
            boolLen = sizeof(reusePort);
            if (::getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reusePort, &boolLen) != 0) continue;

            result = ListeningSocketInfo{
                fd,
                "127.0.0.1",
                ::ntohs(addr.sin_port),
                reuseAddr != 0,
                reusePort != 0,
                (statusFlags & O_NONBLOCK) != 0,
                (fdFlags & FD_CLOEXEC) != 0};
            break;
        }

        ::closedir(dir);
        return result;
    }

    int countListeningTcpSockets()
    {
        int count = 0;
        DIR *dir = ::opendir("/proc/self/fd");
        if (!dir) return count;

        while (dirent *entry = ::readdir(dir))
        {
            if (0 == std::strcmp(entry->d_name, ".") || 0 == std::strcmp(entry->d_name, "..")) continue;

            char *end = nullptr;
            const long parsed = std::strtol(entry->d_name, &end, 10);
            if (!end || *end != '\0' || parsed < 0) continue;

            const int fd = static_cast<int>(parsed);
            int acceptConn = 0;
            socklen_t optLen = sizeof(acceptConn);
            if (::getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &acceptConn, &optLen) != 0) continue;
            if (!acceptConn) continue;

            sockaddr_in addr{};
            socklen_t addrLen = sizeof(addr);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen) != 0) continue;
            if (addr.sin_family != AF_INET) continue;
            ++count;
        }

        ::closedir(dir);
        return count;
    }

    int pollReadableOrHangup(int fd, int timeoutMs)
    {
        struct pollfd pfd
        {
            fd, POLLIN | POLLHUP, 0
        };
        return ::poll(&pfd, 1, timeoutMs);
    }

} // namespace


TEST(AcceptorTest, ConstructorBindsAndListenPublishesSocketOptions)
{
    EventLoop loop;
    Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);

    EXPECT_FALSE(acceptor.listening());

    acceptor.listen();

    EXPECT_TRUE(acceptor.listening());

    auto info = findListeningSocketInfo();
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->fd >= 0);
    EXPECT_EQ(info->ip, "127.0.0.1");
    EXPECT_NE(info->port, 0);
    EXPECT_TRUE(info->reuseAddr);
    EXPECT_FALSE(info->reusePort);
    EXPECT_TRUE(info->nonblocking);
    EXPECT_TRUE(info->cloexec);
}

TEST(AcceptorTest, ListenIsIdempotentAndKeepsSingleListeningSocket)
{
    EventLoop loop;
    Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), true);

    acceptor.listen();
    const int before = countListeningTcpSockets();

    acceptor.listen();
    const int after = countListeningTcpSockets();

    EXPECT_TRUE(acceptor.listening());
    EXPECT_EQ(before, 1);
    EXPECT_EQ(after, 1);
}

TEST(AcceptorTest, AcceptCallbackReceivesPeerAddressAndNonblockingFd)
{
    std::promise<uint16_t> portPromise;
    std::promise<AcceptedConnectionInfo> connectionPromise;
    std::promise<pid_t> loopTidPromise;
    std::promise<void> readyPromise;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               const pid_t loopTid = CurrentThread::tid();
                               Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);

                               acceptor.setNewConnectionCallback([&](int sockfd, const InetAddress &peerAddr)
                                                                 {
                                                                     int fdFlags = ::fcntl(sockfd, F_GETFD);
                                                                     int statusFlags = ::fcntl(sockfd, F_GETFL);
                                                                     connectionPromise.set_value(AcceptedConnectionInfo{
                                                                         sockfd,
                                                                         peerAddr.toIp(),
                                                                         peerAddr.toPort(),
                                                                         CurrentThread::tid(),
                                                                         (statusFlags & O_NONBLOCK) != 0,
                                                                         (fdFlags & FD_CLOEXEC) != 0});
                                                                     loop.quit(); });

                               acceptor.listen();
                               auto info = findListeningSocketInfo();
                               ASSERT_TRUE(info.has_value());
                               portPromise.set_value(info->port);
                               loopTidPromise.set_value(loopTid);
                               readyPromise.set_value();
                               loop.loop(); });

    readyPromise.get_future().wait();
    const uint16_t port = portPromise.get_future().get();
    const pid_t loopTid = loopTidPromise.get_future().get();

    ScopedFd clientFd(createTcpSocket());
    connectToPort(clientFd.get(), port);

    auto connection = connectionPromise.get_future();
    ASSERT_EQ(connection.wait_for(1s), std::future_status::ready);
    const AcceptedConnectionInfo info = connection.get();

    EXPECT_EQ(info.ip, "127.0.0.1");
    EXPECT_NE(info.port, 0);
    EXPECT_EQ(info.tid, loopTid);
    EXPECT_TRUE(info.nonblocking);
    EXPECT_TRUE(info.cloexec);

    ::close(info.fd);
    loopThread.join();
}

TEST(AcceptorTest, AcceptCallbackHandlesMultipleConnections)
{
    std::promise<uint16_t> portPromise;
    std::promise<void> readyPromise;
    std::promise<void> finishedPromise;
    std::mutex mutex;
    std::vector<AcceptedConnectionInfo> accepted;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);

                               acceptor.setNewConnectionCallback([&](int sockfd, const InetAddress &peerAddr)
                                                                 {
                                                                     int fdFlags = ::fcntl(sockfd, F_GETFD);
                                                                     int statusFlags = ::fcntl(sockfd, F_GETFL);
                                                                     {
                                                                         std::lock_guard<std::mutex> lock(mutex);
                                                                         accepted.push_back(AcceptedConnectionInfo{
                                                                             sockfd,
                                                                             peerAddr.toIp(),
                                                                             peerAddr.toPort(),
                                                                             CurrentThread::tid(),
                                                                             (statusFlags & O_NONBLOCK) != 0,
                                                                             (fdFlags & FD_CLOEXEC) != 0});
                                                                     }

                                                                     if (accepted.size() >= 2) loop.quit(); });

                               acceptor.listen();
                               auto info = findListeningSocketInfo();
                               ASSERT_TRUE(info.has_value());
                               portPromise.set_value(info->port);
                               readyPromise.set_value();
                               loop.loop();
                               finishedPromise.set_value(); });

    readyPromise.get_future().wait();
    const uint16_t port = portPromise.get_future().get();

    ScopedFd client1(createTcpSocket());
    ScopedFd client2(createTcpSocket());
    connectToPort(client1.get(), port);
    connectToPort(client2.get(), port);

    ASSERT_EQ(finishedPromise.get_future().wait_for(1s), std::future_status::ready);
    loopThread.join();

    ASSERT_EQ(accepted.size(), 2u);
    EXPECT_EQ(accepted[0].ip, "127.0.0.1");
    EXPECT_EQ(accepted[1].ip, "127.0.0.1");
    EXPECT_NE(accepted[0].port, accepted[1].port);
    EXPECT_TRUE(accepted[0].nonblocking);
    EXPECT_TRUE(accepted[1].nonblocking);
    EXPECT_TRUE(accepted[0].cloexec);
    EXPECT_TRUE(accepted[1].cloexec);

    ::close(accepted[0].fd);
    ::close(accepted[1].fd);
}

TEST(AcceptorTest, NoCallbackClosesAcceptedConnection)
{
    std::promise<uint16_t> portPromise;
    std::promise<void> readyPromise;
    std::promise<EventLoop *> loopPromise;

    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);
                               acceptor.listen();

                               auto info = findListeningSocketInfo();
                               ASSERT_TRUE(info.has_value());
                               portPromise.set_value(info->port);
                               readyPromise.set_value();
                               loopPromise.set_value(&loop);

                               loop.loop(); });

    readyPromise.get_future().wait();
    const uint16_t port = portPromise.get_future().get();

    ScopedFd clientFd(createTcpSocket());
    connectToPort(clientFd.get(), port);

    ASSERT_GT(pollReadableOrHangup(clientFd.get(), 1000), 0);

    char byte = '\0';
    EXPECT_EQ(::recv(clientFd.get(), &byte, sizeof(byte), 0), 0);

    EventLoop *loop = loopPromise.get_future().get();
    loop->quit();
    loopThread.join();
}

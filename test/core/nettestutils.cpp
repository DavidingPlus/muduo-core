#include "nettestutils.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <future>
#include <stdexcept>
#include <utility>

#include "eventloop.h"
#include "timestamp.h"


namespace NetTestUtils
{

    ScopedFd::~ScopedFd()
    {
        if (m_fd >= 0) ::close(m_fd);
    }

    ScopedFd::ScopedFd(ScopedFd &&other) noexcept : m_fd(other.release()) {}

    ScopedFd &ScopedFd::operator=(ScopedFd &&other) noexcept
    {
        if (this != &other) reset(other.release());
        return *this;
    }

    int ScopedFd::release()
    {
        const int fd = m_fd;
        m_fd = -1;
        return fd;
    }

    void ScopedFd::reset(int fd)
    {
        if (m_fd >= 0) ::close(m_fd);
        m_fd = fd;
    }

    ScopedEnvVar::ScopedEnvVar(const char *name) : m_name(name)
    {
        const char *value = ::getenv(m_name.c_str());
        if (value)
        {
            m_hadValue = true;
            m_value = value;
        }
    }

    ScopedEnvVar::~ScopedEnvVar()
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

    Pipe::Pipe()
    {
        if (0 != ::pipe(m_fds)) throw std::runtime_error("pipe failed");
    }

    Pipe::~Pipe()
    {
        if (m_fds[0] >= 0) ::close(m_fds[0]);
        if (m_fds[1] >= 0) ::close(m_fds[1]);
    }

    Timestamp StubPoller::poll(int, ChannelList *)
    {
        return Timestamp::Now();
    }

    int createBlockingTcpSocket()
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);
        return fd;
    }

    int createEventFd()
    {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        EXPECT_GE(fd, 0);
        return fd;
    }

    void writeEventFd(int fd, uint64_t value)
    {
        ASSERT_EQ(::write(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    }

    void readEventFd(int fd)
    {
        uint64_t value = 0;
        ASSERT_EQ(::read(fd, &value, sizeof(value)), static_cast<ssize_t>(sizeof(value)));
    }

    void connectToPort(int fd, uint16_t port)
    {
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = ::htons(port);
        ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr), 1);
        ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)), 0);
    }

    int connectClientToPort(uint16_t port)
    {
        const int fd = createBlockingTcpSocket();
        EXPECT_GE(fd, 0);
        if (fd < 0) return fd;

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = ::htons(port);
        if (1 != ::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr))
        {
            ADD_FAILURE() << "inet_pton failed";
            ::close(fd);
            return -1;
        }
        if (0 != ::connect(fd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)))
        {
            ADD_FAILURE() << "connect failed: " << errno;
            ::close(fd);
            return -1;
        }
        return fd;
    }

    int getSocketOption(int fd, int level, int option)
    {
        int value = -1;
        socklen_t len = sizeof(value);
        EXPECT_EQ(::getsockopt(fd, level, option, &value, &len), 0);
        EXPECT_EQ(len, sizeof(value));
        return value;
    }

    uint16_t getBoundPort(int fd)
    {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len), 0);
        EXPECT_EQ(len, sizeof(addr));
        return ::ntohs(addr.sin_port);
    }

    sockaddr_in makeSockAddr(const char *ip, uint16_t port)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        EXPECT_EQ(::inet_pton(AF_INET, ip, &addr.sin_addr), 1);
        return addr;
    }

    ConnectedTcpSockets createConnectedTcpSockets()
    {
        ConnectedTcpSockets sockets;

        sockets.listener.reset(::socket(AF_INET, SOCK_STREAM, 0));
        if (!sockets.listener.valid())
        {
            ADD_FAILURE() << "Failed to create listening socket";
            return sockets;
        }

        int reuseAddr = 1;
        if (0 != ::setsockopt(sockets.listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)))
        {
            ADD_FAILURE() << "setsockopt(SO_REUSEADDR) failed: " << errno;
            return sockets;
        }

        sockaddr_in listenAddr{};
        listenAddr.sin_family = AF_INET;
        listenAddr.sin_port = ::htons(0);
        if (1 != ::inet_pton(AF_INET, "127.0.0.1", &listenAddr.sin_addr))
        {
            ADD_FAILURE() << "inet_pton failed";
            return sockets;
        }
        if (0 != ::bind(sockets.listener.get(), reinterpret_cast<sockaddr *>(&listenAddr), sizeof(listenAddr)))
        {
            ADD_FAILURE() << "bind failed: " << errno;
            return sockets;
        }
        if (0 != ::listen(sockets.listener.get(), 16))
        {
            ADD_FAILURE() << "listen failed: " << errno;
            return sockets;
        }

        socklen_t listenLen = sizeof(listenAddr);
        if (0 != ::getsockname(sockets.listener.get(), reinterpret_cast<sockaddr *>(&listenAddr), &listenLen))
        {
            ADD_FAILURE() << "getsockname(listener) failed: " << errno;
            return sockets;
        }
        sockets.port = ::ntohs(listenAddr.sin_port);

        sockets.client.reset(createBlockingTcpSocket());
        if (!sockets.client.valid())
        {
            ADD_FAILURE() << "Failed to create client socket";
            return sockets;
        }

        sockaddr_in clientConnectAddr{};
        clientConnectAddr.sin_family = AF_INET;
        clientConnectAddr.sin_port = ::htons(sockets.port);
        if (1 != ::inet_pton(AF_INET, "127.0.0.1", &clientConnectAddr.sin_addr))
        {
            ADD_FAILURE() << "inet_pton(client) failed";
            return sockets;
        }
        if (0 != ::connect(sockets.client.get(), reinterpret_cast<sockaddr *>(&clientConnectAddr), sizeof(clientConnectAddr)))
        {
            ADD_FAILURE() << "connect failed: " << errno;
            return sockets;
        }

        sockaddr_in peerAddr{};
        socklen_t peerLen = sizeof(peerAddr);
        sockets.server.reset(::accept4(sockets.listener.get(), reinterpret_cast<sockaddr *>(&peerAddr), &peerLen, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (!sockets.server.valid())
        {
            ADD_FAILURE() << "accept4 failed: " << errno;
            return sockets;
        }

        sockaddr_in localAddr{};
        socklen_t localLen = sizeof(localAddr);
        if (0 != ::getsockname(sockets.server.get(), reinterpret_cast<sockaddr *>(&localAddr), &localLen))
        {
            ADD_FAILURE() << "getsockname(server) failed: " << errno;
            return sockets;
        }

        sockets.localAddr = InetAddress(localAddr);
        sockets.peerAddr = InetAddress(peerAddr);
        return sockets;
    }

    short waitForFdEvent(int fd, short events, int timeoutMs)
    {
        struct pollfd pfd
        {
            fd, events, 0
        };

        const int rc = ::poll(&pfd, 1, timeoutMs);
        EXPECT_GE(rc, 0);
        if (rc <= 0) return 0;
        return pfd.revents;
    }

    std::string readExactly(int fd, size_t expectedBytes, int timeoutMs)
    {
        std::string data(expectedBytes, '\0');
        size_t total = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (total < expectedBytes)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;

            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const short revents = waitForFdEvent(fd, POLLIN | POLLHUP, static_cast<int>(remaining));
            if (!(revents & (POLLIN | POLLHUP))) break;

            const ssize_t n = ::read(fd, data.data() + total, expectedBytes - total);
            if (n <= 0) break;
            total += static_cast<size_t>(n);
        }

        data.resize(total);
        return data;
    }

    std::string readUntilEof(int fd, int timeoutMs)
    {
        std::string data;
        char buffer[4096];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (true)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;

            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const short revents = waitForFdEvent(fd, POLLIN | POLLHUP, static_cast<int>(remaining));
            if (!(revents & (POLLIN | POLLHUP))) break;

            const ssize_t n = ::read(fd, buffer, sizeof(buffer));
            if (0 == n) break;
            if (n < 0)
            {
                if (EINTR == errno) continue;
                break;
            }
            data.append(buffer, static_cast<size_t>(n));
        }

        return data;
    }

    std::map<int, ListeningSocketInfo> snapshotListeningTcpSockets()
    {
        std::map<int, ListeningSocketInfo> sockets;
        DIR *dir = ::opendir("/proc/self/fd");
        if (!dir) return sockets;

        while (dirent *entry = ::readdir(dir))
        {
            if (0 == std::strcmp(entry->d_name, ".") || 0 == std::strcmp(entry->d_name, "..")) continue;

            char *end = nullptr;
            const long parsed = std::strtol(entry->d_name, &end, 10);
            if (!end || *end != '\0' || parsed < 0) continue;

            const int fd = static_cast<int>(parsed);
            int acceptConn = 0;
            socklen_t acceptLen = sizeof(acceptConn);
            if (::getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &acceptConn, &acceptLen) != 0 || !acceptConn) continue;

            sockaddr_in addr{};
            socklen_t addrLen = sizeof(addr);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen) != 0) continue;
            if (AF_INET != addr.sin_family) continue;

            char ip[INET_ADDRSTRLEN] = {0};
            if (!::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) continue;

            int reuseAddr = 0;
            int reusePort = 0;
            socklen_t boolLen = sizeof(reuseAddr);
            if (::getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, &boolLen) != 0) continue;
            boolLen = sizeof(reusePort);
            if (::getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reusePort, &boolLen) != 0) continue;

            const int fdFlags = ::fcntl(fd, F_GETFD);
            const int statusFlags = ::fcntl(fd, F_GETFL);
            if (fdFlags < 0 || statusFlags < 0) continue;

            sockets.emplace(fd, ListeningSocketInfo{
                                    fd,
                                    ip,
                                    ::ntohs(addr.sin_port),
                                    reuseAddr != 0,
                                    reusePort != 0,
                                    (statusFlags & O_NONBLOCK) != 0,
                                    (fdFlags & FD_CLOEXEC) != 0});
        }

        ::closedir(dir);
        return sockets;
    }

    std::optional<ListeningSocketInfo> findNewListeningSocket(const std::map<int, ListeningSocketInfo> &before)
    {
        const auto after = snapshotListeningTcpSockets();
        for (const auto &[fd, info] : after)
        {
            if (before.find(fd) == before.end()) return info;
        }
        return std::nullopt;
    }

    std::optional<ListeningSocketInfo> findListeningSocketInfo(std::string_view ip)
    {
        const auto sockets = snapshotListeningTcpSockets();
        for (const auto &[fd, info] : sockets)
        {
            (void)fd;
            if (ip.empty() || info.ip == ip) return info;
        }
        return std::nullopt;
    }

    int countListeningTcpSockets(std::string_view ip)
    {
        int count = 0;
        const auto sockets = snapshotListeningTcpSockets();
        for (const auto &[fd, info] : sockets)
        {
            (void)fd;
            if (ip.empty() || info.ip == ip) ++count;
        }
        return count;
    }

    TcpConnectionPtr createEstablishedConnection(EventLoop *loop, ConnectedTcpSockets &sockets, const std::function<void(const TcpConnectionPtr &)> &configure)
    {
        std::promise<TcpConnectionPtr> created;
        const int serverFd = sockets.server.release();

        loop->runInLoop([&]()
                        {
                            auto conn = std::make_shared<TcpConnection>(loop, "test-conn", serverFd, sockets.localAddr, sockets.peerAddr);
                            configure(conn);
                            conn->connectEstablished();
                            created.set_value(conn); });

        return created.get_future().get();
    }

    void destroyConnection(EventLoop *loop, const TcpConnectionPtr &conn)
    {
        std::promise<void> done;
        loop->runInLoop([&]()
                        {
                            conn->connectDestroyed();
                            done.set_value(); });
        EXPECT_EQ(done.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    }

    ScopedFd createTempFileWithContent(const std::string &content)
    {
        char path[] = "/tmp/muduo-core-tcpconnection-XXXXXX";
        const int fd = ::mkstemp(path);
        EXPECT_GE(fd, 0);
        if (fd >= 0)
        {
            EXPECT_EQ(::unlink(path), 0);
            if (static_cast<ssize_t>(content.size()) != ::write(fd, content.data(), content.size()))
            {
                ADD_FAILURE() << "Failed to write temp file content";
                ::close(fd);
                return ScopedFd();
            }
            if (0 != ::lseek(fd, 0, SEEK_SET))
            {
                ADD_FAILURE() << "Failed to rewind temp file";
                ::close(fd);
                return ScopedFd();
            }
        }
        return ScopedFd(fd);
    }

    std::shared_ptr<TcpServer> createAndStartServer(EventLoop *loop, int threadNum, const std::function<void(TcpServer &)> &configure, uint16_t *port)
    {
        std::promise<std::shared_ptr<TcpServer>> created;
        std::promise<uint16_t> portPromise;
        const auto before = snapshotListeningTcpSockets();

        loop->runInLoop([&]()
                        {
                            auto server = std::make_shared<TcpServer>(loop, InetAddress(0, "127.0.0.1"), "TcpServerTest");
                            server->setThreadNum(threadNum);
                            configure(*server);
                            server->start();

                            auto info = findNewListeningSocket(before);
                            ASSERT_TRUE(info.has_value());
                            ASSERT_EQ(info->ip, "127.0.0.1");
                            portPromise.set_value(info->port);
                            created.set_value(server); });

        *port = portPromise.get_future().get();
        return created.get_future().get();
    }

    void destroyServerOnLoop(EventLoop *loop, std::shared_ptr<TcpServer> &server)
    {
        std::promise<void> destroyed;
        auto serverToDestroy = std::move(server);

        loop->runInLoop([serverToDestroy = std::move(serverToDestroy), &destroyed]() mutable
                        {
                            serverToDestroy.reset();
                            destroyed.set_value(); });

        EXPECT_EQ(destroyed.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    }

} // namespace NetTestUtils

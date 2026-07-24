#ifndef _MUDUO_CORE_TEST_NETTESTUTILS_H_
#define _MUDUO_CORE_TEST_NETTESTUTILS_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <netinet/in.h>
#include <poll.h>
#include <sys/types.h>

#include "globalmacros.h"

#include "inetaddress.h"
#include "poller.h"
#include "tcpconnection.h"
#include "tcpserver.h"
#include "timestamp.h"


namespace NetTestUtils
{

    // 统一管理测试里临时创建的 fd，避免 ASSERT/EXPECT 提前返回时泄漏。
    class ScopedFd
    {

        CLASS_NONCOPYABLE(ScopedFd)

    public:

        ScopedFd() = default;

        explicit ScopedFd(int fd) : m_fd(fd) {}

        ~ScopedFd()
        {
            if (m_fd >= 0) ::close(m_fd);
        }

        ScopedFd(const ScopedFd &) = delete;
        ScopedFd &operator=(const ScopedFd &) = delete;

        ScopedFd(ScopedFd &&other) noexcept : m_fd(other.release()) {}

        ScopedFd &operator=(ScopedFd &&other) noexcept
        {
            if (this != &other) reset(other.release());
            return *this;
        }

        int get() const { return m_fd; }

        bool valid() const { return m_fd >= 0; }

        int release();

        void reset(int fd = -1);


    private:

        int m_fd = -1;
    };

    // 测试环境里临时覆盖环境变量，析构时恢复现场，避免污染其他用例。
    class ScopedEnvVar
    {

        CLASS_NONCOPYABLE(ScopedEnvVar)

    public:

        explicit ScopedEnvVar(const char *name);

        ~ScopedEnvVar();


    private:

        std::string m_name;

        std::string m_value;

        bool m_hadValue = false;
    };

    // 为 Buffer 相关测试提供最轻量的本地读写端，不依赖网络栈。
    class Pipe
    {

        CLASS_NONCOPYABLE(Pipe)

    public:

        Pipe();

        ~Pipe();

        int readEnd() const { return m_fds[0]; }

        int writeEnd() const { return m_fds[1]; }


    private:

        int m_fds[2] = {-1, -1};
    };

    // 某些断言只关心“集合相等而不关心顺序”，这里统一转成 set 比较。
    template <typename T>
    std::unordered_set<T> toSet(const std::vector<T> &values) { return std::unordered_set<T>(values.begin(), values.end()); }

    // Poller 抽象类的最小可测实现，只给接口契约测试使用。
    class StubPoller : public Poller
    {

    public:

        explicit StubPoller(EventLoop *loop) : Poller(loop) {}

        Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;

        void updateChannel(Channel *channel) override { (void)channel; }

        void removeChannel(Channel *channel) override { (void)channel; }

        void track(Channel *channel) { m_channels[channel->fd()] = channel; }
    };

    // 一组已经完成握手的本地 TCP 连接，方便直接构造 TcpConnection 用例。
    struct ConnectedTcpSockets
    {
        ScopedFd listener;

        ScopedFd client;

        ScopedFd server;

        InetAddress localAddr;

        InetAddress peerAddr;

        uint16_t port = 0;
    };

    // 从内核侧抓到的监听 socket 状态快照，便于直接断言 listen 结果。
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

    // accept 回调里回传的连接信息，既包含 peer 地址，也包含 fd 属性。
    struct AcceptedConnectionInfo
    {
        int fd = -1;

        std::string ip;

        uint16_t port = 0;

        pid_t tid = 0;

        bool nonblocking = false;

        bool cloexec = false;
    };

    int createBlockingTcpSocket();

    inline int createTcpSocket() { return createBlockingTcpSocket(); }

    int createEventFd();

    void writeEventFd(int fd, uint64_t value = 1);

    void readEventFd(int fd);

    void connectToPort(int fd, uint16_t port);

    int connectClientToPort(uint16_t port);

    int getSocketOption(int fd, int level, int option);

    uint16_t getBoundPort(int fd);

    sockaddr_in makeSockAddr(const char *ip, uint16_t port);

    ConnectedTcpSockets createConnectedTcpSockets();

    short waitForFdEvent(int fd, short events, int timeoutMs);

    inline int pollReadableOrHangup(int fd, int timeoutMs) { return waitForFdEvent(fd, POLLIN | POLLHUP, timeoutMs); }

    std::string readExactly(int fd, size_t expectedBytes, int timeoutMs = 2000);

    std::string readUntilEof(int fd, int timeoutMs = 2000);

    // 扫描当前进程的监听 fd，用于验证 listen()/start() 是否真的把 socket 暴露给了内核。
    std::map<int, ListeningSocketInfo> snapshotListeningTcpSockets();

    std::optional<ListeningSocketInfo> findNewListeningSocket(const std::map<int, ListeningSocketInfo> &before);

    std::optional<ListeningSocketInfo> findListeningSocketInfo(std::string_view ip = "127.0.0.1");

    int countListeningTcpSockets(std::string_view ip = {});

    // 这些 helper 负责把 TcpConnection/TcpServer 的创建与销毁切回正确的 EventLoop 线程。
    TcpConnectionPtr createEstablishedConnection(EventLoop *loop, ConnectedTcpSockets &sockets, const std::function<void(const TcpConnectionPtr &)> &configure);

    void destroyConnection(EventLoop *loop, const TcpConnectionPtr &conn);

    ScopedFd createTempFileWithContent(const std::string &content);

    std::shared_ptr<TcpServer> createAndStartServer(EventLoop *loop, int threadNum, const std::function<void(TcpServer &)> &configure, uint16_t *port);

    void destroyServerOnLoop(EventLoop *loop, std::shared_ptr<TcpServer> &server);

} // namespace NetTestUtils

#endif

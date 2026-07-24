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
#include <unistd.h>

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

    public:

        ScopedFd() = default;

        explicit ScopedFd(int fd) : m_fd(fd) {}

        ~ScopedFd();

        ScopedFd(const ScopedFd &) = delete;

        ScopedFd &operator=(const ScopedFd &) = delete;

        ScopedFd(ScopedFd &&other) noexcept : m_fd(other.release()) {}

        ScopedFd &operator=(ScopedFd &&other) noexcept;

        // 返回当前持有的 fd，不转移所有权。
        int get() const { return m_fd; }

        // 当前是否实际持有一个可关闭的 fd。
        bool valid() const { return m_fd >= 0; }

        // 放弃 fd 所有权并返回原始值，调用者负责后续 close。
        int release();

        // 用新的 fd 替换当前持有的对象；若旧 fd 有效，会先关闭旧 fd。
        void reset(int fd = -1);


    private:

        int m_fd = -1;
    };

    // 测试环境里临时覆盖环境变量，析构时恢复现场，避免污染其他用例。
    class ScopedEnvVar
    {

    public:

        explicit ScopedEnvVar(const char *name);

        ~ScopedEnvVar();

        ScopedEnvVar(const ScopedEnvVar &) = delete;

        ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;


    private:

        std::string m_name;

        std::string m_value;

        bool m_hadValue = false;
    };

    // 为 Buffer 相关测试提供最轻量的本地读写端，不依赖网络栈。
    class Pipe
    {

    public:

        Pipe();

        ~Pipe();

        Pipe(const Pipe &) = delete;

        Pipe &operator=(const Pipe &) = delete;

        // 返回管道读端，适合喂给 Buffer::readFd() 这类读取路径。
        int readEnd() const { return m_fds[0]; }

        // 返回管道写端，用于向被测对象注入输入数据。
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

        // 返回一个有效时间戳即可，用于验证 Poller 抽象接口本身的契约。
        Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;

        void updateChannel(Channel *channel) override { (void)channel; }

        void removeChannel(Channel *channel) override { (void)channel; }

        // 手工把 Channel 放进基类映射，便于测试 hasChannel() 这类纯接口逻辑。
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

    // 创建最基础的阻塞 TCP socket，失败时通过 gtest 断言暴露问题。
    int createBlockingTcpSocket();

    // 与 createBlockingTcpSocket() 语义一致，保留给测试代码更短的调用名。
    inline int createTcpSocket() { return createBlockingTcpSocket(); }

    // 创建带 NONBLOCK/CLOEXEC 的 eventfd，主要供 Channel/Poller/EventLoop 用例使用。
    int createEventFd();

    // 向 eventfd 写入计数，制造一个可读事件。
    void writeEventFd(int fd, uint64_t value = 1);

    // 读空 eventfd 中的计数，避免后续轮询误判“还有未消费事件”。
    void readEventFd(int fd);

    // 用现有 fd 主动连接到本机指定端口，失败直接让测试用例失败。
    void connectToPort(int fd, uint16_t port);

    // 创建一个新的客户端 socket 并连接到本机指定端口，成功后由调用方接管 fd 生命周期。
    int connectClientToPort(uint16_t port);

    // 直接从内核读取 socket option，验证 Socket setter 是否真正生效。
    int getSocketOption(int fd, int level, int option);

    // 读取 socket 实际绑定的本地端口，便于客户端回连。
    uint16_t getBoundPort(int fd);

    // 生成指定 ip/port 的 sockaddr_in，便于 InetAddress 相关用例构造输入。
    sockaddr_in makeSockAddr(const char *ip, uint16_t port);

    // 创建一组已经完成本地握手的 listener/client/server socket，便于直接进入已连接态测试。
    ConnectedTcpSockets createConnectedTcpSockets();

    // 等待 fd 上出现指定事件；超时返回 0，出错会通过 EXPECT 暴露给用例。
    short waitForFdEvent(int fd, short events, int timeoutMs);

    // 等待“可读或挂断”这两个测试里最常见的状态组合。
    inline int pollReadableOrHangup(int fd, int timeoutMs) { return waitForFdEvent(fd, POLLIN | POLLHUP, timeoutMs); }

    // 尝试从 fd 精确读取 expectedBytes 字节；若超时或对端提前关闭，则返回实际读到的内容。
    std::string readExactly(int fd, size_t expectedBytes, int timeoutMs = 2000);

    // 持续读取直到 EOF 或超时，用于验证 shutdown/close 等收尾路径。
    std::string readUntilEof(int fd, int timeoutMs = 2000);

    // 扫描当前进程的监听 fd，用于验证 listen()/start() 是否真的把 socket 暴露给了内核。
    std::map<int, ListeningSocketInfo> snapshotListeningTcpSockets();

    // 对比前后快照，找出本轮新出现的监听 socket。
    std::optional<ListeningSocketInfo> findNewListeningSocket(const std::map<int, ListeningSocketInfo> &before);

    // 在当前进程监听 socket 中查找一个匹配 ip 的项；默认只看 127.0.0.1。
    std::optional<ListeningSocketInfo> findListeningSocketInfo(std::string_view ip = "127.0.0.1");

    // 统计当前进程监听中的 TCP socket 数量，可按 ip 过滤。
    int countListeningTcpSockets(std::string_view ip = {});

    // 这些 helper 负责把 TcpConnection/TcpServer 的创建与销毁切回正确的 EventLoop 线程。
    // createEstablishedConnection() 会在目标 loop 线程里构造 TcpConnection、执行用户配置并调用 connectEstablished()。
    TcpConnectionPtr createEstablishedConnection(EventLoop *loop, ConnectedTcpSockets &sockets, const std::function<void(const TcpConnectionPtr &)> &configure);

    // 在连接所属 loop 线程里执行 connectDestroyed()，保证和真实销毁路径的线程语义一致。
    void destroyConnection(EventLoop *loop, const TcpConnectionPtr &conn);

    // 创建一个匿名临时文件并写入给定内容，常用于 sendFile() 类测试。
    ScopedFd createTempFileWithContent(const std::string &content);

    // 在目标 loop 线程里创建并启动 TcpServer，同时把实际监听端口回传给调用方。
    std::shared_ptr<TcpServer> createAndStartServer(EventLoop *loop, int threadNum, const std::function<void(TcpServer &)> &configure, uint16_t *port);

    // 在 server 所属 loop 线程里释放最后一个 shared_ptr，保证析构线程与实际运行线程一致。
    void destroyServerOnLoop(EventLoop *loop, std::shared_ptr<TcpServer> &server);

} // namespace NetTestUtils

#endif

#include "acceptor.h"

#include "eventloop.h"
#include "inetaddress.h"
#include "logger.h"

#include <sys/socket.h>


int Acceptor::createSocketNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0) LOG_FATAL("{}:{}:{} listen socket create err: {}", __FILE__, __FUNCTION__, __LINE__, errno);
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : m_mainLoop(loop), m_acceptSocket(createSocketNonblocking()), m_acceptChannel(m_mainLoop, m_acceptSocket.fd())
{
    m_acceptSocket.setReuseAddr(true);
    m_acceptSocket.setReusePort(reuseport);
    m_acceptSocket.bindAddress(listenAddr);

    // TcpServer::start() -> Acceptor.listen()。如果有新用户连接，要执行一个回调，流程是 accept -> connfd -> 打包成 Channel -> 唤醒 subLoop。
    // mainLoop 监听到有事件发生 -> m_acceptChannel(listenfd) -> 执行该回调函数。
    m_acceptChannel.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    // Acceptor 属于 mainLoop，析构必须发生在 mainLoop 所在线程。

    // 把从 Poller 中感兴趣的事件删除掉。
    m_acceptChannel.disableAll();
    // 调用 EventLoop->removeChannel() -> Poller->removeChannel()，把 Poller 的 ChannelMap 对应的部分删除。
    m_acceptChannel.remove();
}

void Acceptor::listen()
{
    // 这里结束的时候没有把 m_listening 置为 false，因为 Acceptor 的语义是一个服务器监听器，一旦启动，就一直监听，直到服务器关闭。它不是一个可暂停/恢复的对象。
    if (m_listening) return;

    m_listening = true;

    // 必须先调用 listen()，再将 Channel 注册到 Poller。如果先注册 Channel 再 listen，此时 fd 还没有处于监听状态，Reactor 中记录的事件状态与 socket 实际状态不一致，导致语义紊乱，可能会出现问题。

    // 调用 listen，使 socket 进入监听状态。listen() 本身不会阻塞，它只是通知内核，当前 socket 用于监听客户端连接，并创建对应的连接等待队列。后续客户端完成 TCP 三次握手后，连接会进入 accept 队列，等待 accept() 获取。
    m_acceptSocket.listen();
    // 将监听 socket 注册到 EventLoop 的 Poller 中。listenfd 对服务器来说是一个可读事件，当有新的客户端连接到达时，epoll_wait() 会返回该事件，然后由 Channel 回调 Acceptor::handleRead() 执行 accept()。注意：这里注册的是事件通知，并不会主动等待连接。真正等待事件发生的是 EventLoop::loop() 中的 epoll_wait()。
    m_acceptChannel.enableReading();
}

void Acceptor::handleRead()
{
    // listen socket 收到可读事件。对监听 socket 来说，可读意味着存在新的连接请求。调用 accept 获取已建立连接的 connfd。
    InetAddress peerAddr;
    int connfd = m_acceptSocket.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (m_newConnectionCallback)
        {
            // 轮询找到 subLoop，唤醒并分发当前的新客户端的 Channel。
            m_newConnectionCallback(connfd, peerAddr);
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        // 非阻塞 accept：没有更多连接，正常退出（EAGAIN、EWOULDBLOCK）。被信号打断，重新尝试（EINTR）。
        if (EAGAIN == errno || EWOULDBLOCK == errno || EINTR == errno) return;

        // 遇到错误。
        LOG_ERROR("{}:{}:{} accept err: {}", __FILE__, __FUNCTION__, __LINE__, errno);

        // 进程打开文件描述符达到限制。
        if (EMFILE == errno) LOG_ERROR("{}:{}:{} sockfd reached limit", __FILE__, __FUNCTION__, __LINE__);
    }
}

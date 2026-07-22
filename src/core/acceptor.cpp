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
    : m_loop(loop), m_acceptSocket(createSocketNonblocking()), m_acceptChannel(m_loop, m_acceptSocket.fd())
{
    m_acceptSocket.setReuseAddr(true);
    m_acceptSocket.setReusePort(true);
    m_acceptSocket.bindAddress(listenAddr);

    // TcpServer::start() -> Acceptor.listen()。如果有新用户连接，要执行一个回调，流程是 accept -> connfd -> 打包成 Channel -> 唤醒 subLoop。
    // mainLoop 监听到有事件发生 -> m_acceptChannel(listenfd) -> 执行该回调函数。
    m_acceptChannel.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    // 把从 Poller 中感兴趣的事件删除掉。
    m_acceptChannel.disableAll();
    // 调用 EventLoop->removeChannel() -> Poller->removeChannel()，把 Poller 的 ChannelMap 对应的部分删除。
    m_acceptChannel.remove();
}

void Acceptor::listen()
{
}

void Acceptor::handleRead()
{
}

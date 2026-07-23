#include "tcpconnection.h"

#include "logger.h"
#include "netutils.h"
#include "timestamp.h"


TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr)
    : m_loop(NetUtils::CheckLoopNotNull(loop)), m_name(name), m_socket(new Socket(sockfd)), m_channel(new Channel(m_loop, sockfd)), m_localAddr(localAddr), m_peerAddr(peerAddr)
{
    // 下面给 channel 设置相应的回调函数，poller 给 channel 通知感兴趣的事件发生了 channel 会回调相应的回调函数。
    m_channel->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    m_channel->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    m_channel->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    m_channel->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[{}] at fd={}", m_name, sockfd);

    m_socket->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[{}] at fd={} state={}", m_name, m_channel->fd(), static_cast<int>(m_state.load()));
}

void TcpConnection::send(const std::string &buf)
{
}

void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count)
{
}

void TcpConnection::shutdown()
{
}

void TcpConnection::connectEstablished()
{
    setState(StateE::kConnected);

    // 设置 m_channel 的 tie 机制保证 Channel 和 TcpConnection 的生命周期语义。
    m_channel->tie(shared_from_this());
    // 向 poller 注册 channel 的 EPOLLIN 读事件。
    m_channel->enableReading();

    // 新连接建立，执行回调。
    m_connectionCallback(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    // TODO 这个条件判断是啥意思？
    if (StateE::kConnected == m_state)
    {
        setState(StateE::kDisconnected);

        // 把 channel 的所有感兴趣的事件从 poller 中删除掉。
        m_channel->disableAll();

        // 执行连接状态变化回调。
        m_connectionCallback(shared_from_this());
    }

    // 把 channel 从 poller 中删除掉。
    m_channel->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = m_inputBuffer.readFd(m_channel->fd(), &savedErrno);
    // 有数据到达。
    if (n > 0)
    {
        // 已建立连接的用户有可读事件发生了，调用用户传入的回调操作 m_messageCallback，shared_from_this 用于安全地获取 TcpConnection 的智能指针。
        m_messageCallback(shared_from_this(), &m_inputBuffer, receiveTime);
    }
    // 客户端断开。
    else if (0 == n)
    {
        handleClose();
    }
    // 出错了。
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead() failed");

        handleError();
    }
}

void TcpConnection::handleWrite()
{
}

void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose() fd={} state={}", m_channel->fd(), static_cast<int>(m_state.load()));
    setState(StateE::kDisconnected);
    m_channel->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    // 执行连接状态变化回调。
    m_connectionCallback(connPtr);
    // 执行关闭连接的回调。
    // 具体执行的是 TcpServer::removeConnection() 回调（由 TcpServer::newConnection() 创建并初始化），最后会执行到 TcpConnection::connectDestroyed() 回调。会执行一系列删除操作，因此必须放在最后一句。
    m_closeCallback(connPtr);
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof(optval);
    int err = 0;

    // 从 socket 获取内核记录的错误状态。getsockopt() 成功时，optval 保存 socket 错误码；失败时，通过 errno 获取系统调用本身的错误。
    if (0 == ::getsockopt(m_channel->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen))
    {
        err = optval;
    }
    else
    {
        err = errno;
    }

    LOG_ERROR("TcpConnection::handleError name:{} - SO_ERROR:{}", m_name, err);
}

void TcpConnection::sendInLoop(const void *data, size_t len)
{
}

void TcpConnection::shutdownInLoop()
{
}

void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count)
{
}

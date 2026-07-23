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
}

void TcpConnection::connectDestroyed()
{
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
}

void TcpConnection::handleWrite()
{
}

void TcpConnection::handleClose()
{
}

void TcpConnection::handleError()
{
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

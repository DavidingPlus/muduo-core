#include "tcpconnection.h"

#include "logger.h"
#include "netutils.h"
#include "timestamp.h"
#include "eventloop.h"


TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr)
    : m_loop(NetUtils::CheckLoopNotNull(loop)), m_name(name), m_socket(new Socket(sockfd)), m_channel(new Channel(m_loop, sockfd)), m_localAddr(localAddr), m_peerAddr(peerAddr)
{
    // 不能在构造函数中用 shared_from_this()，因为对象还在构造中，shared_ptr 的控制块通常还没建立好。同时这里直接绑定 this 是安全的，等后面 connectEstablished() 里再通过 tie(shared_from_this()) 保护生命周期。只有 TcpConnection 还活着时，Channel 才会真正分发事件回调。
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
    // 和构造函数中不同，这里应该用 shared_from_this()。因为发送逻辑可能被投递到别的线程执行，所以这里要先用 shared_from_this() 把对象“保活”。否则如果回调稍后才执行，而 TcpConnection 已经被释放，就会访问悬空对象。
    if (connected())
    {
        m_loop->runInLoop(std::bind(&TcpConnection::sendInLoop, shared_from_this(), buf.c_str(), buf.size()));
    }
    else
    {
        LOG_ERROR("TcpConnection::send() - not connected");
    }
}

void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count)
{
    if (connected())
    {
        m_loop->runInLoop(std::bind(&TcpConnection::sendFileInLoop, shared_from_this(), fileDescriptor, offset, count));
    }
    else
    {
        LOG_ERROR("TcpConnection::sendFile() - not connected");
    }
}

void TcpConnection::shutdown()
{
    if (connected())
    {
        // shutdown() 不会立即销毁 TcpConnection，而是发起 TCP 半关闭流程。TCP 连接的读写方向是独立的，关闭写端后仍然可以继续读取对端数据。因此需要先进入 kDisconnecting 状态，表示：本端已经请求关闭发送方向；但连接仍然有效，可能还有数据需要接收；等待剩余数据发送完成以及对端关闭后，最终进入 kDisconnected。
        setState(StateE::kDisconnecting);
        m_loop->runInLoop(std::bind(&TcpConnection::shutdownInLoop, shared_from_this()));
    }
}

void TcpConnection::connectEstablished()
{
    setState(StateE::kConnected);

    // 这是第一次安全使用 shared_from_this() 的地方：TcpConnection 已经被 shared_ptr 接管。tie 只做“临时保活”，防止事件分发时对象先析构。
    m_channel->tie(shared_from_this());
    // 向 poller 注册 channel 的 EPOLLIN 读事件。
    m_channel->enableReading();

    // 新连接建立，执行回调。
    m_connectionCallback(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    // TODO 这个条件判断是啥意思？
    if (connected())
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
    ssize_t n = m_inputBuffer.readFd(m_channel->fd(), savedErrno);
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
        LOG_ERROR("TcpConnection::handleRead() failed, errno:{}", errno);

        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if (m_channel->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = m_outputBuffer.writeFd(m_channel->fd(), savedErrno);
        if (n > 0)
        {
            // Buffer 的语义设计是 writeFd 只负责发送数据，真正消费 retrieve 数据由调用者决定。
            m_outputBuffer.retrieve(n);
            // 如果发送缓冲区已经空了，说明当前发送任务完成。
            if (0 == m_outputBuffer.readableBytes())
            {
                // 停止监听写事件。
                m_channel->disableWriting();
                // TcpConnection 对象在其所在的 subloop 中，向 pendingFunctors 中投递回调。
                if (m_writeCompleteCallback) m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
                // 如果处于正在断开连接 kDisconnecting 状态，说明用户之前调用过 shutdown()（也应该由用户调用）。shutdown() 的调用链不会立即关闭 TCP 写端，而是先进入 kDisconnecting 状态，等待 m_outputBuffer 中剩余数据全部发送完成，避免未发送数据丢失。
                // 具体理解：conn->send("hello"); conn->shutdown()。此时：state == kDisconnecting；m_outputBuffer = "hello"。用户手动调用 shutdown() 进而 shutdownInLoop() 时，由于还有待发送数据，shutdownInLoop() 中的 !m_channel->isWriting() 判断不会满足，不会立即发送 TCP FIN。后续 handleWrite() 将 m_outputBuffer 中的数据全部发送完成后，会调用 m_channel->disableWriting() 停止监听 EPOLLOUT，此时说明发送缓冲区已经清空，可以再次进入 shutdownInLoop()，安全关闭 TCP 写端，发送 FIN，实现延迟优雅关闭。
                if (StateE::kDisconnecting == m_state) shutdownInLoop();
            }
        }
        else
        {
            errno = savedErrno;
            LOG_ERROR("TcpConnection::handleWrite() writeFd failed, errno:{}", errno);
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd={} is down, no more writing", m_channel->fd());
    }
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

void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count)
{
}

void TcpConnection::shutdownInLoop()
{
    // 此私有函数由共有函数 shutdown() 通过 runInLoop() 投递，已经保证执行事件循环的语义，并且执行逻辑非常简单，因此可以直接执行业务逻辑而无需再次投递。
    // 若当前 m_channel 还处于正在写状态，无法半关闭。当 m_outputBuffer 的数据全部向外发送完成，可以切换为本端写半关闭。
    if (!m_channel->isWriting()) m_socket->shutdownWrite();
}

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
    // send() 只允许在 kConnected 状态下进入，含义是：一旦用户已经发起 shutdown()，状态变为 kDisconnecting，就不再接受新的业务发送请求。但这并不影响之前已经进入发送流程的数据继续发送完毕；那部分收尾工作由 sendInLoop()/handleWrite() 负责。
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
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 只有 kDisconnected 才表示“连接已经彻底不能写了”。kDisconnecting 不能在这里直接拦掉，因为它表示的是“优雅关闭进行中”：用户可能刚执行完 send() 就立刻 shutdown()，此时状态虽然已切到 kDisconnecting，但这批数据仍然需要继续发完，随后才能真正 shutdownWrite()。
    if (StateE::kDisconnected == m_state) LOG_ERROR("disconnected, give up writing");

    // 只有“当前发送通道完全空闲”时，才适合直接尝试 write：
    // 1. !m_channel->isWriting()：说明当前没有注册 EPOLLOUT，发送收尾流程没有进行中。因为用户可能多次 send()，这会导致 isWriting() 为 true。
    // 2. 0 == m_outputBuffer.readableBytes()：说明用户态发送缓冲区里没有前一次遗留的待发送数据。
    // 这两个条件合起来表示“发送通道当前空闲”，可以走一次直接写 socket 的快路径，即先 ::write(fd, data, len)，如果一次写完，最好，连 m_outputBuffer 都不用进。反过来说，只要之前 send() 遗留了未发送完的数据，或者已经进入等待 EPOLLOUT 的异步发送阶段，新数据都不能直接 write，否则会有插队风险，破坏应用层期望的发送顺序；此时只能追加到 m_outputBuffer 末尾排队。
    // 进一步说，存不存在 m_channel->isWriting() && 0 == m_outputBuffer.readableBytes() 这种状态呢？可能有，这个状态可能也满足条件，但是绝对不是稳定可控的状态，可能只是一个瞬时态。isWriting() 代表 Reactor 侧的发送流程状态，outputBuffer 代表用户态缓存状态，二者是不同层面的信息。把两个条件都写上，才能明确表达：只要异步发送流程还没彻底结束，新数据就不能直接插队写 socket，我们不能冒语义不清晰的险。
    if (!m_channel->isWriting() && 0 == m_outputBuffer.readableBytes())
    {
        nwrote = ::write(m_channel->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            // 既然在这里数据全部发送完成，就不用再给 channel 设置 EPOLLOUT 事件了。
            if (0 == remaining && m_writeCompleteCallback) m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
        }
        else
        {
            // write() 失败时返回 -1，但后续 m_outputBuffer.append((char *)data + nwrote, remaining); 代码复用了 nwrote 来计算“还有多少数据没发出去”以及“剩余数据从哪里开始追加到 m_outputBuffer”。这里把 nwrote 改成 0，明确表达“本次一个字节都没写出去（因为失败）”，保证后续语句的正确性，含义就是：如果这是可恢复情况（例如非阻塞 socket 当前暂时不可写），那就把整块原始数据都追加进发送缓冲区排队。
            nwrote = 0;
            // EWOULDBLOCK / EAGAIN 表示非阻塞 socket 当前不可写，常见原因是内核发送缓冲区暂时已满；这不是连接级错误，而是“稍后再试”。只有 errno 不是这两种“可恢复的暂时不可写”错误时，才按真正的写失败来处理。
            if (EWOULDBLOCK != errno && EAGAIN != errno)
            {
                LOG_ERROR("TcpConnection::sendInLoop()");

                // faultError 表示“这次发送失败是否已经上升为连接级故障”。如果只是 EWOULDBLOCK / EAGAIN，remaining 仍可以进入 m_outputBuffer，等待 EPOLLOUT 再继续发送。但下面两种错误说明连接本身已经坏了：
                // 1. EPIPE：对端读端已关闭，本端继续写，继续缓存待发送数据已经没有意义。
                // 2. ECONNRESET：连接被对端复位，TCP 链路已经不可继续使用。
                // 因此将 faultError 置为 true，告诉后续逻辑不要再把 remaining 追加到发送缓冲区。
                if (EPIPE == errno || ECONNRESET == errno) faultError = true;
            }
        }
    }

    // 能走到这一步，说明当前这一次 write 并没有把数据全部发送出去，剩余的数据需要保存到缓冲区当中，然后给 channel 注册 EPOLLOUT 事件，等待 Poller 下次 IO 通知相应的 sock->channel，调用 channel 对应注册的 m_writeCallback 回调方法，channel 的 m_writeCallback 实际上就是 TcpConnection 设置的 handleWrite 回调，把发送缓冲区 m_outputBuffer 的内容全部发送完成。
    if (!faultError && remaining > 0)
    {
        // oldLen 表示追加本次 remaining 之前，发送缓冲区里已经积压的待发送数据量。
        size_t oldLen = m_outputBuffer.readableBytes();
        // 高水位回调采用“越线时通知一次”的边沿语义（可类比 epoll 的边沿触发理解）：
        // 1. oldLen < m_highWaterMark：追加前还没越线，说明这是首次跨过阈值。
        // 2. oldLen + remaining >= m_highWaterMark：这次追加后达到或超过高水位。
        // 这样可以避免缓冲区已经很大时，每次 send() 都重复触发回调，形成通知轰炸。注意：高水位回调只是把“发送积压已进入危险区”的事实通知上层，并不是自动背压机制；上层业务仍需自行决定是否限流、暂停生产、丢弃消息、记录告警或关闭连接。
        if (m_highWaterMarkCallback && oldLen < m_highWaterMark && oldLen + remaining >= m_highWaterMark) m_loop->queueInLoop(std::bind(m_highWaterMarkCallback, shared_from_this(), oldLen + remaining));

        m_outputBuffer.append(reinterpret_cast<const char *>(data) + nwrote, remaining);

        // 这里一定要注册 channel 的写事件，否则 poller 不会给 channel 通知 EPOLLOUT，也保证上面快路径的判断条件正确。
        if (!m_channel->isWriting()) m_channel->enableWriting();
    }
}

void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count)
{
}

void TcpConnection::shutdownInLoop()
{
    // 此私有函数由共有函数 shutdown() 通过 runInLoop() 投递，已经保证执行事件循环的语义，并且执行逻辑非常简单，因此可以直接执行业务逻辑而无需再次投递。
    // 若当前 m_channel 还处于正在写状态，无法半关闭。当 m_outputBuffer 的数据全部向外发送完成，可以切换为本端写半关闭。这正是 kDisconnecting 的收尾语义：不再接收新发送，但允许旧数据发送完成；只有发送队列清空后，才真正关闭写端。
    if (!m_channel->isWriting()) m_socket->shutdownWrite();
}

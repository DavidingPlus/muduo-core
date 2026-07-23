#include "tcpserver.h"

#include "logger.h"
#include "eventloop.h"
#include "eventloopthread.h"
#include "eventloopthreadpool.h"
#include "netutils.h"
#include "tcpconnection.h"


TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &name, Option option)
    : m_mainLoop(NetUtils::CheckLoopNotNull(loop)), m_ipPort(listenAddr.toIpPort()), m_name(name), m_acceptor(std::make_unique<Acceptor>(m_mainLoop, listenAddr, Option::kReusePort == option)), m_threadPool(new EventLoopThreadPool(m_mainLoop, m_name))
{
    // 当有新用户连接时，Acceptor 类中绑定的 m_acceptChannel 会有读事件发生，执行 handleRead() 调用 TcpServer::newConnection() 回调。
    // 对应 Acceptor::handleRead() 中的 m_newConnectionCallback(connfd, peerAddr);
    m_acceptor->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    // TcpServer 销毁时，需要关闭并销毁所有已经建立的 TcpConnection。TcpConnection 不是由 TcpServer 所在线程直接销毁，而是需要回到 TcpConnection 所属的 EventLoop(IO 线程) 中执行 connectDestroyed()。
    for (auto &item : m_connections)
    {
        // 复制一份 shared_ptr，延长 TcpConnection 生命周期。后续会 reset() m_connections 中保存的 shared_ptr，但是 runInLoop() 中的 bind 对象仍然持有一份 shared_ptr，保证在 IO 线程执行 connectDestroyed() 前 TcpConnection 不会析构。
        TcpConnectionPtr conn(item.second);
        // 把原始的 shared_ptr 对象复位，移除 TcpServer 对该连接的所有权，现在我们刚创建的栈空间的 TcpConnectionPtr conn 指向该对象，当 conn 出了其作用域，即可释放智能指针指向的对象。
        item.second.reset();
        // 将 TcpConnection 的销毁操作转移到其所属 EventLoop 线程执行。注意：bind 会复制 conn(shared_ptr)，因此即使当前析构函数结束，TcpConnection 对象仍然存活，直到目标 IO 线程执行完 connectDestroyed() 后引用计数归零，最终释放对象。
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }
}

void TcpServer::setThreadNum(int numThreads)
{
    m_numThreads = numThreads;
    m_threadPool->setThreadNum(m_numThreads);
}

void TcpServer::start()
{
    // 防止一个 TcpServer 对象被 start 多次。所以多次调用没有副作用，线程安全。用原子标志保证只启动一次。
    // 可以对原子变量进行 fetch_xxx 运算操作，原子修改自身，但返回旧值。因此这里可以用来判断多次调用，例如：if (0 == m_started.fetch_add(1))
    // 同理，exchange() 的作用是原子地交换值，并返回旧值的函数，用在这里刚合适。第一次调用时：old == false，设置为 true，返回 false，进入启动流程。后续调用时：old == true，仍保持 true，返回 true，不再重复启动。
    if (!m_started.exchange(true))
    {
        // 先启动 subLoop 线程池，创建并初始化 IO 工作线程。后续接收到的新连接会被分配给这些 subLoop 处理。
        m_threadPool->start(m_threadInitCallback);
        // subLoop 准备完成后，再让 mainLoop 开始监听 accept socket。这样可以保证新连接到来时，已经存在可用的 IO 工作线程处理连接。
        // 注意：m_mainLoop 作为 TcpServer 的主控制 EventLoop，不参与实际的 IO 事件调度。与 subLoop 不同，mainLoop 不调用 loop() 进入 Reactor 循环，因为 loop() 会驱动 Poller 监听文件描述符并分发 Channel 事件，这属于 IO 工作线程的职责。mainLoop 主要用于跨线程提交任务，例如通过 runInLoop() 将 Acceptor::listen() 投递到 mainLoop 所在线程执行，保证监听 socket 的初始化操作在线程安全的上下文中完成。subLoop 线程池中的 EventLoop 才负责真正运行事件循环，处理连接建立后的读写事件。这也是为什么 EventLoop 提供了 loop() 和 runInLoop()/queueInLoop() 两类函数，对应了 EventLoop 处理的两类回调操作。
        m_mainLoop->runInLoop(std::bind(&Acceptor::listen, m_acceptor.get()));
    }
}

void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 在网络编程中，当一个服务器端的 socket 接收到一个新的连接请求时，它会通过 accept() 函数创建一个新的 socket 描述符（sockfd），用于与新连接的客户端进行通信。这个新的 socket 描述符代表了服务器端与客户端的连接通道。在 TcpServer::newConnection() 方法中，sockfd 是这个新建立的连接的 socket 描述符。虽然这个连接已经建立，但是为了进行数据的发送和接收，服务器需要知道这个连接的具体网络地址信息，即这个 socket 绑定的本地服务端 IP 地址和端口号。这些信息对于服务器来说是非常重要的。
    // 1. 日志记录：记录服务端在当前连接中使用的 ip 和端口号，便于调试和追踪问题。
    // 2. 支持多网卡环境下的网络服务，服务端可能有多个网卡，绑定了不同的 ip 地址。如果服务端监听的是 0.0.0.0（即监听所有网卡上的地址），那么 accept 返回的 sockfd 实际绑定的本地 IP 和端口可能会因具体的客户端请求而异。使用 getsockname() 可以告诉我们此时建立连接的网卡的服务端的 ip 地址和 port。
    // 3. 初始化 Tcpconnection 对象。
    // 4. 防御性编程：确保绑定的 ip 和 port，通过 getsockname 确定没有问题。

    // 轮询算法，选择一个 subLoop 来管理 connfd 对应的 channel。
    EventLoop *ioLoop = m_threadPool->getNextLoop();

    // m_nextConnId 没有设置为原子类是因为 TcpServer::newConnection() 只在 mainloop 中执行，不涉及线程安全问题。
    std::string connName = fmt::format("{}-{}#{}", m_name, m_ipPort, m_nextConnId++); // MyServer-127.0.0.1:8888#1

    LOG_INFO("TcpServer::newConnection [{}] - new connection [{}] from {}", m_name, connName, peerAddr.toIpPort());

    // 监听到 listen 读事件以后，Acceptor::handleRead() 会调用 accept()，并且将本机连接通信用的 sockfd 和连接方的 peerAddr 传递进来。
    // 通过 sockfd 获取绑定的本机的 ip 地址和端口信息。
    sockaddr_in local{};
    socklen_t addrlen = sizeof(local);
    if (::getsockname(sockfd, reinterpret_cast<sockaddr *>(&local), &addrlen) < 0) LOG_ERROR("TcpServer::newConnection() getsockname failed");

    InetAddress localAddr(local);

    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    m_connections[connName] = conn;
    // 下面的回调都是用户设置给 TcpServer -> TcpConnection 的，被保存在 TcpConnection 的成员变量中。Channel 绑定的则是 TcpConnection 自己设置的四个，handleRead，handleWrite，handleClose，handleError 回调，这四个回调会调用 TcpServer 设置的函数，并做一些其他处理。
    conn->setConnectionCallback(m_connectionCallback);
    conn->setMessageCallback(m_messageCallback);
    conn->setWriteCompleteCallback(m_writeCompleteCallback);
    // 设置了如何关闭连接的回调。
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // 向 ioLoop 投递建立好连接的 TcpConnection::connectEstablished() 回调函数。
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    m_mainLoop->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [{}] - connection {}", m_name, conn->name());

    m_connections.erase(conn->name());

    EventLoop *ioLoop = conn->getLoop();
    // 这里选择使用 queueInLoop() 延迟执行 TcpConnection::connectDestroyed()，是为了让当前事件循环中正在处理的关闭事件流程先完整结束，再在下一轮 EventLoop 中安全地清理 TcpConnection 相关资源。
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}

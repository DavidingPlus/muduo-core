#include "tcpserver.h"

#include "logger.h"
#include "eventloop.h"
#include "eventloopthread.h"
#include "eventloopthreadpool.h"


EventLoop *TcpServer::CheckLoopNotNull(EventLoop *loop)
{
    if (!loop) LOG_FATAL("{}:{}:{} mainLoop is null!", __FILE__, __FUNCTION__, __LINE__);
    return loop;
}

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &name, Option option)
    : m_mainLoop(CheckLoopNotNull(loop)), m_ipPort(listenAddr.toIpPort()), m_name(name), m_acceptor(std::make_unique<Acceptor>(loop, listenAddr, Option::kReusePort == option)), m_threadPool(new EventLoopThreadPool(m_mainLoop, m_name))
{
    // 当有新用户连接时，Acceptor 类中绑定的 m_acceptChannel 会有读事件发生，执行 handleRead() 调用 TcpServer::newConnection() 回调。
    // 对应 Acceptor::handleRead() 中的 m_newConnectionCallback(connfd, peerAddr);
    m_acceptor->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    // for (auto &item : m_connections)
    // {
    //     TcpConnectionPtr conn(item.second);
    //     // 把原始的智能指针复位，让栈空间的 TcpConnectionPtr conn 指向该对象，当 conn 出了其作用域，即可释放智能指针指向的对象。
    //     item.second.reset();
    //     // 销毁连接
    //     conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    // }
}

void TcpServer::start()
{
    // 防止一个 TcpServer 对象被 start 多次。所以多次调用没有副作用，线程安全。
    // 原子变量进行 fetch_xxx 运算操作，修改自身，但返回旧值。因此这里可以用来判断多次调用。
    if (0 == m_started.fetch_add(1))
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
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
}

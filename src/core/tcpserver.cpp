#include "tcpserver.h"

#include "logger.h"
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

void TcpServer::setThreadNum(int numThreads)
{
}

void TcpServer::start()
{
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

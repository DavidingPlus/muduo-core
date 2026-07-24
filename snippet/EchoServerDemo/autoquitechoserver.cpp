#include "autoquitechoserver.h"


void AutoQuitEchoServer::onConnection(const TcpConnectionPtr &conn)
{
    // 保留基类的连接日志输出，避免派生类把公共行为重复写一遍。
    BaseEchoServer::onConnection(conn);

    if (conn->connected())
    {
        ++m_totalConnections;
        ++m_aliveConnections;
        return;
    }

    const int alive = --m_aliveConnections;

    // 只有在“所有预期客户端都连接过”且“当前没有活跃连接”时才退出。这样可以避免某个客户端提前断开就把服务端直接停掉。
    if (m_totalConnections.load() < m_expectedClients || 0 != alive) return;

    EventLoop *eventLoop = loop();

    // 通过 queueInLoop 回到服务端所属线程退出，避免在任意回调线程里直接 quit。
    eventLoop->queueInLoop([eventLoop]()
                           { eventLoop->quit(); });
}

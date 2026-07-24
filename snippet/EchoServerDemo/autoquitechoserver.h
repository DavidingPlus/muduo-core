#pragma once

#include <atomic>
#include <string>

#include "baseechoserver.h"


// AutoQuitEchoServer 在 BaseEchoServer 的基础上增加“自动退出”语义：当累计接待的客户端数量达到 expectedClients，且当前已无存活连接时，服务端会在所属 EventLoop 中排队执行 quit()，用于测试这类场景。
class AutoQuitEchoServer : public BaseEchoServer
{

public:

    AutoQuitEchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name, int expectedClients, int threadNum = 3) : BaseEchoServer(loop, addr, name, threadNum), m_expectedClients(expectedClients) {}


protected:

    // 复用基类日志，再叠加连接计数和自动退出逻辑。
    void onConnection(const TcpConnectionPtr &conn) override;


private:

    // 期望接待的客户端总数。达到该数量后才允许触发自动退出。
    const int m_expectedClients = 0;

    // totalConnections 统计“曾经建立过”的连接数量；
    std::atomic_int m_totalConnections = 0;

    // aliveConnections 统计“当前还活着”的连接数量。
    std::atomic_int m_aliveConnections = 0;
};

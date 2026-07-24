#include <string>

#include "logger.h"
#include "eventloop.h"
#include "tcpserver.h"
#include "tcpconnection.h"


class EchoServer
{

public:

    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
        : m_server(loop, addr, name), m_loop(loop)
    {
        // 注册回调函数。
        m_server.setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));

        m_server.setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的 subLoop 线程数量。
        m_server.setThreadNum(3);
    }

    void start() { m_server.start(); }


private:

    // 连接建立或断开的回调函数。
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : {}", conn->peerAddress().toIpPort());
        }
        else
        {
            LOG_INFO("Connection DOWN : {}", conn->peerAddress().toIpPort());
        }
    }

    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn, Buffer &buf, const Timestamp &time)
    {
        std::string msg = buf.retrieveAllAsString();
        conn->send(msg);
        // conn->shutdown();   // 关闭写端，底层响应 EPOLLHUP -> 执行 m_closeCallback。
    }


    TcpServer m_server;
    EventLoop *m_loop = nullptr;
};


int main()
{
    EventLoop loop;
    InetAddress addr(8080);
    EchoServer server(&loop, addr, "EchoServer");
    server.start();
    loop.loop();
}

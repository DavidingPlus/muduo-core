#ifndef _MUDUO_CORE_ACCEPTOR_H_
#define _MUDUO_CORE_ACCEPTOR_H_

#include "globalmacros.h"

#include "socket.h"
#include "channel.h"

#include <functional>

class EventLoop;
class InetAddress;


// Accetpor 封装了服务器监听套接字 fd 以及相关处理方法。
class Acceptor
{

    CLASS_NONCOPYABLE(Acceptor)

public:

    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &)>;


    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);

    ~Acceptor();

    // 设置新连接的回调函数。
    void setNewConnectionCallback(const NewConnectionCallback &cb) { m_newConnectionCallback = cb; }

    // 判断是否在监听。
    bool listening() const { return m_listening; }

    // 监听本地端口。
    void listen();


private:

    // 创建一个非阻塞的 socket。
    static int CreateSocketNonblocking();


    // 处理新用户的连接事件。
    void handleRead();


    // 监听套接字的 fd 由哪个 EventLoop 负责循环监听以及处理相应事件，其实就是 mainLoop。
    EventLoop *m_mainLoop = nullptr;

    // 服务器监听套接字的 socket。
    Socket m_acceptSocket;

    // Channel 对象，把 acceptSocket 及其感兴趣事件和事件对应的处理函数都封装进去。
    Channel m_acceptChannel;

    // 新连接对象的回调函数。TcpServer 构造函数中将 TcpServer::newConnection() 函数注册给了这个成员变量。TcpServer::newConnection() 函数的功能是公平的选择一个 subEventLoop，并把已经接受的连接分发给这个 subEventLoop。
    NewConnectionCallback m_newConnectionCallback;

    // 是否在监听。
    bool m_listening = false;
};

#endif

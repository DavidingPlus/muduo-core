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
    static int createSocketNonblocking();


    // 处理新用户的连接事件。
    void handleRead();


    // Acceptor 用的就是用户定义的那个 baseLoop，也称作 mainLoop。
    EventLoop *m_loop = nullptr;

    // 专门用于接收新连接的 socket。
    Socket m_acceptSocket;

    // 专门用于监听新连接的 channel。
    Channel m_acceptChannel;

    // 新连接的回调函数。
    NewConnectionCallback m_newConnectionCallback;

    // 是否在监听。
    bool m_listening = false;
};

#endif

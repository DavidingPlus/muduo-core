#ifndef _MUDUO_CORE_TCPSERVER_H_
#define _MUDUO_CORE_TCPSERVER_H_

#include "globalmacros.h"

#include "inetaddress.h"

#include <memory>
#include <string>

class EventLoop;


// TcpServer -> Acceptor -> 有一个新用户连接，通过 accept 函数拿到 connfd -> TcpConnection 设置回调 -> 设置到 Channel -> Poller -> Channel 回调。
// TcpConnection 类用 shared_ptr 来管理，继承自 enable_shared_from_this。这是因为其生命周期模糊：可能在连接断开时，还有其他地方持有它的引用，贸然 delete 会造成悬空指针。只有确保其他地方没有持有该对象的引用的时候，才能安全地销毁对象。
class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{

    CLASS_NONCOPYABLE(TcpConnection)

public:

    TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr);

    ~TcpConnection();


private:

    // 这里是 mainLoop 还是 subLoop 由 TcpServer 中创建的线程数决定。若为多 Reactor，该 loop 指向 subLoop，若为单 Reactor，该 loop 指向 mainLoop。
    EventLoop *m_loop;

    const std::string m_name;

    const InetAddress m_localAddr;

    const InetAddress m_peerAddr;
};


#endif

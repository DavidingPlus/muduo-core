#ifndef _MUDUO_CORE_SOCKET_H_
#define _MUDUO_CORE_SOCKET_H_

#include "globalmacros.h"

#include <unistd.h>

class InetAddress;


// Socket 类只有一个成员 sockfd，该类的作用就是用于管理 TCP 连接对应的 sockfd 的生命周期（析构的时候 close 该 sockfd），以及提供一些函数来修改 sockfd 上的选项，比如 Nagel 算法、设置地址复用等。
// TODO 目前仅支持 TCP。
class Socket
{

    CLASS_NONCOPYABLE(Socket)

public:

    explicit Socket(int sockfd) : m_sockfd(sockfd) {}

    ~Socket() { ::close(m_sockfd); }

    int fd() const { return m_sockfd; }

    // 绑定 sockfd，就是调用 ::bind 函数。
    void bindAddress(const InetAddress &localaddr);

    // 监听 sockfd，就是调用 ::listen 函数。
    void listen();

    // 接受连接。peeraddr 是待连接的客户端地址指针，连接成功后，内核会把客户端的信息填进去。
    int accept(InetAddress *peeraddr);

    // 设置半关闭。
    void shutdownWrite();

    // 设置 Nagel 算法。true 代表禁用 Nagle 算法。
    void setTcpNoDelay(bool on);

    // 设置地址复用。
    void setReuseAddr(bool on);

    // 设置端口复用。
    void setReusePort(bool on);

    // 设置长连接。
    void setKeepAlive(bool on);


private:

    const int m_sockfd = -1;
};


#endif

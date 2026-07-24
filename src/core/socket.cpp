#include "socket.h"

#include "logger.h"
#include "inetaddress.h"

#include <cstring>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/tcp.h>


void Socket::bindAddress(const InetAddress &localaddr)
{
    if (0 != ::bind(m_sockfd, (sockaddr *)localaddr.getSockAddr(), sizeof(sockaddr_in))) LOG_FATAL("Socket::bindAddress sockfd: {} failed", m_sockfd);
}

void Socket::listen()
{
    if (0 != ::listen(m_sockfd, 1024)) LOG_FATAL("Socket::listen() sockfd: {} failed", m_sockfd);
}

int Socket::accept(InetAddress *peeraddr)
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    // accept4() 是 accept() 的扩展版本，可以在创建新连接 socket 的同时设置一些属性，避免后续再调用 fcntl() 修改。
    // SOCK_NONBLOCK：设置新连接 socket 为非阻塞。
    // SOCK_CLOEXEC：它表示 close-on-exec，也就是当调用 exec() 替换当前进程镜像时，自动关闭这个 fd。
    int connfd = ::accept4(m_sockfd, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0) peeraddr->setSockAddr(addr);
    return connfd;
}

void Socket::shutdownWrite()
{
    // TCP 是全双工通信，读写两个方向可以独立关闭。
    // shutdown(SHUT_WR) 只关闭写方向：
    // 1. 向对端发送 FIN，表示本端不会再发送数据；
    // 2. 仍然可以继续接收对端数据。
    // 与 close() 不同，close() 会同时关闭读写两个方向。Reactor 网络库通常使用半关闭，保证发送缓冲区数据发送完成后再优雅关闭连接。
    if (::shutdown(m_sockfd, SHUT_WR) < 0) LOG_ERROR("Socket::shutdownWrite() error");
}

void Socket::setTcpNoDelay(bool on)
{
    // TCP_NODELAY 用于禁用 Nagle 算法。Nagle 算法用于减少网络上传输的小数据包数量。将 TCP_NODELAY 设置为 1 可以禁用该算法，允许小数据包立即发送。
    int optval = on ? 1 : 0;
    ::setsockopt(m_sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

void Socket::setReuseAddr(bool on)
{
    // SO_REUSEADDR 允许一个套接字强制绑定到一个已被其他套接字使用的端口。这对于需要重启并绑定到相同端口的服务器应用程序非常有用。
    // TCP 连接关闭后，连接四元组（源 IP、源端口、目的 IP、目的端口）可能会进入 TIME_WAIT 状态，防止旧数据影响新的连接。此时如果服务器立即重启并 bind 相同地址，可能会因为旧连接未释放而失败。开启 SO_REUSEADDR 后，允许 socket 重新绑定处于 TIME_WAIT 状态的地址，方便服务器程序快速重启。
    int optval = on ? 1 : 0;
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Socket::setReusePort(bool on)
{
    // SO_REUSEPORT 允许同一主机上的多个套接字绑定到相同的端口号。这对于在多个线程或进程之间负载均衡传入连接非常有用。
    // 默认情况下，一个 IP 地址和端口只能被一个 socket 绑定。开启 SO_REUSEPORT 后，允许多个 socket 同时绑定相同的 IP 和端口。常用于多进程或多线程服务器模型：每个工作线程/进程拥有独立的监听 socket，内核负责将新的连接分配给不同的 socket，实现负载均衡。
    int optval = on ? 1 : 0;
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

void Socket::setKeepAlive(bool on)
{
    // SO_KEEPALIVE 启用在已连接的套接字上定期传输消息。如果另一端没有响应，则认为连接已断开并关闭。这对于检测网络中失效的对等方非常有用。
    int optval = on ? 1 : 0;
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

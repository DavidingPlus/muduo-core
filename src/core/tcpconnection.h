#ifndef _MUDUO_CORE_TCPCONNECTION_H_
#define _MUDUO_CORE_TCPCONNECTION_H_

#include "globalmacros.h"

#include "inetaddress.h"
#include "callbacks.h"
#include "acceptor.h"
#include "socket.h"
#include "buffer.h"

#include <memory>
#include <string>
#include <atomic>

class EventLoop;
class Timestamp;


// TcpServer -> Acceptor -> 有一个新用户连接，通过 accept 函数拿到 connfd -> TcpConnection 设置回调 -> 设置到 Channel -> Poller -> Channel 回调。
// TcpConnection 类用 shared_ptr 来管理，继承自 enable_shared_from_this。这是因为其生命周期模糊：可能在连接断开时，还有其他地方持有它的引用，贸然 delete 会造成悬空指针。只有确保其他地方没有持有该对象的引用的时候，才能安全地销毁对象。
class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{

    CLASS_NONCOPYABLE(TcpConnection)

public:

    TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr);

    ~TcpConnection();

    EventLoop *getLoop() const { return m_loop; }

    const std::string &name() const { return m_name; }

    const InetAddress &localAddress() const { return m_localAddr; }

    const InetAddress &peerAddress() const { return m_peerAddr; }

    bool connected() const { return StateE::kConnected == m_state; }

    // send()、sendFile() 和 shutdown() 内部都会调用和事件循环相关的 xxxInLoop() 的私有函数。

    // 用户主动发送数据。
    void send(const std::string &data);

    void sendFile(int fileDescriptor, off_t offset, size_t count);

    // 关闭半连接。
    void shutdown();

    void setConnectionCallback(const ConnectionCallback &cb) { m_connectionCallback = cb; }

    void setMessageCallback(const MessageCallback &cb) { m_messageCallback = cb; }

    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { m_writeCompleteCallback = cb; }

    void setCloseCallback(const CloseCallback &cb) { m_closeCallback = cb; }

    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark) { m_highWaterMarkCallback = cb, m_highWaterMark = highWaterMark; }

    // 连接建立。
    void connectEstablished();

    // 连接销毁。
    void connectDestroyed();


private:

    enum class StateE
    {
        kDisconnected, // 已经断开连接。
        kConnecting,   // 正在连接。
        kConnected,    // 已连接。
        kDisconnecting // 正在断开连接。这里不是“连接已不可用”，而是“本端已经请求关闭写方向，进入优雅关闭阶段”。第一，不再接受新的业务发送请求，因此外部 send()/sendFile() 会被 connected() 判断拦住。第二，允许之前已经进入发送流程的数据继续发完，避免丢失 m_outputBuffer 中的待发送数据。第三，待输出缓冲区清空后，再由 shutdownInLoop() 真正 shutdownWrite() 发送 FIN。
    };


    void setState(StateE state) { m_state = state; }

    // 读是对服务器而言的，当对端客户端有数据到达，服务器端检测到 EPOLLIN，就会触发该 fd 上的回调，handleRead() 取读走对端发来的数据。
    // TcpConnection 对 EPOLLIN 事件的处理函数。与发送流程不同，接收数据不是由用户主动发起的，应用程序无法调用类似 recv() "请求读取数据"。handleRead() 负责的是："响应网络事件，读取对端发送的数据，并交给业务处理"，它处理的是已经到达内核的数据，而不是主动向对端请求数据。
    void handleRead(const Timestamp &receiveTime);

    // TcpConnection 对 EPOLLOUT 事件的处理函数。当 sendInLoop() 发现一次 write 无法发送全部数据时，会把剩余数据保存到 m_outputBuffer，并向 epoll 注册写事件。当 TCP 内核发送缓冲区重新可写时，此时 handleWrite() 被调用，将 m_outputBuffer，并向 中缓存的数据继续写入 socket。handleWrite() 负责的是："处理之前未发送完成的数据，完成异步发送流程"，它处理的是 m_outputBuffer 中的历史数据，而不是新的业务发送请求。
    void handleWrite();

    // 处理连接关闭的回调。
    void handleClose();

    // 处理连接运行中发生错误的回调。Channel 在处理通道事件 handleEvent 时，如果发生错误（即不是读取数据，也不是写数据完成）调用。链路：Channel::handleEvent() -> 检测到错误，调用Channel::errorCallback() -> TcpConnection::handleError()。
    void handleError();

    // 发送数据。应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，而且设置了水位回调。
    // TcpConnection 的主动发送入口。当业务层调用 TcpConnection::send() 时，最终会进入该函数。它负责将用户产生的新数据发送到 TCP socket。这里要区分两类数据：第一，“新的业务发送请求”：由 send()/sendFile() 触发，走 sendInLoop()。第二，“之前没发完的历史数据”：保存在 m_outputBuffer 中，等待 EPOLLOUT 到来后由 handleWrite() 继续发送。sendInLoop() 主要处理第 1 类；handleWrite() 主要处理第 2 类。
    void sendInLoop(const std::string &data);

    void sendFileInLoop(int fileDescriptor, off_t offset, size_t count);

    void shutdownInLoop();


    // 这里是 mainLoop 还是 subLoop 由 TcpServer 中创建的线程数决定。若为多 Reactor，该 loop 指向 subLoop，若为单 Reactor，该 loop 指向 mainLoop。
    EventLoop *m_loop = nullptr;

    const std::string m_name;

    const InetAddress m_localAddr;

    const InetAddress m_peerAddr;

    // 因为 TcpConnection 建立的时候已经是 TcpServer 收到连接请求了，因此状态是正在连接。
    std::atomic<StateE> m_state = StateE::kConnecting;

    // 连接是否在监听读事件。同理因为 TcpConnection 建立的时候已经是 TcpServer 收到连接请求了，因此肯定正在监听读事件。
    bool m_reading = true;

    // 这里和 Acceptor 类似。区别是 Acceptor 对应 mainloop，TcpConnection -> subloop，二者的定位和分工不同。
    std::unique_ptr<Socket> m_socket;

    std::unique_ptr<Channel> m_channel;

    // 这些回调 TcpServer 也有，用户通过写入 TcpServer 注册，TcpServer 再将注册的回调传递给 TcpConnection，TcpConnection 再将回调注册到 Channel 中。

    // 连接状态变化回调。当 TcpConnection 的生命周期状态发生变化时调用，用于通知上层业务。它不是“新连接到来”的回调，而是整个连接状态机变化的通知。典型触发场景：
    // 1. connectEstablished()：kConnecting -> kConnected。表示连接建立完成，通知业务初始化会话资源。
    // 2. connectDestroyed()：kConnected -> kDisconnected。表示连接已经关闭，通知业务释放会话资源。
    ConnectionCallback m_connectionCallback;

    // TCP 连接收到数据后调用的消息回调。TcpConnection 负责从 socket 读取数据并存入 m_inputBuffer，然后通过该回调将收到的数据交给上层业务处理。
    MessageCallback m_messageCallback;

    // 消息发送完成以后的回调。
    WriteCompleteCallback m_writeCompleteCallback;

    // 关闭连接的回调。
    CloseCallback m_closeCallback;

    // 高水位回调。
    HighWaterMarkCallback m_highWaterMarkCallback;

    // 高水位阈值，这里设置为 64 M。
    size_t m_highWaterMark = 64 * 1024 * 1024;

    // 接收数据的缓冲区。
    Buffer m_inputBuffer;

    // 发送数据的缓冲区。用户 send 向 m_outputBuffer 发。
    Buffer m_outputBuffer;
};


#endif

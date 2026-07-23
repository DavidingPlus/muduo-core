#ifndef _MUDUO_CORE_TCPSERVER_H_
#define _MUDUO_CORE_TCPSERVER_H_

#include "globalmacros.h"

#include "inetaddress.h"
#include "callbacks.h"
#include "acceptor.h"

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

class EventLoop;
class EventLoopThreadPool;


// TcpServer 类管理 TcpConnetcion，供用户直接使用，生命周期由用户控制。用户只需要设置好 callback，然后调用 start() 即可。下图是 TcpServer 新建立连接的相关函数调用顺序。EventLoop::loop() -> EPollPoller::poll()。当 epoll_wait 被唤醒，并且唤醒的是 listeningfd 可读，就会返回所有就绪的 Channel 的事件，此时就会调用 Accpetor::handleRead() 方法，创建 TcpConnection 对象，后面会处理其他回调事件。
// 这里 channel 有几种：
// 1. Eventloop 注册的 Channel。eventfd 主要是唤醒 epoll_wait，当在执行 pending 的时候又来了新的回调防止阻塞太久。
// 2. Acceptor 注册的 Channel，其目的是就是 listenfd 新连接可到的时候，调用 TcpServer::newConnection() 创建 TcpConnection 对象。
// 3. TcpConnection 会注册四种 fd 感兴趣的事件，也就是 TcpConnection 对应的 Channel。
class TcpServer
{

    CLASS_NONCOPYABLE(TcpServer)

public:

    using ThreadInitCallback = std::function<void(EventLoop *)>;


    enum class Option
    {
        kNoReusePort, // 不允许重用本地端口。
        kReusePort,   // 允许重用本地端口。
    };


    TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &name, Option option = Option::kNoReusePort);

    ~TcpServer();

    void setConnectionCallback(const ConnectionCallback &cb) { m_connectionCallback = cb; }

    void setMessageCallback(const MessageCallback &cb) { m_messageCallback = cb; }

    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { m_writeCompleteCallback = cb; }

    void setThreadInitCallback(const ThreadInitCallback &cb) { m_threadInitCallback = cb; }

    // 设置底层 subloop 的个数。
    void setThreadNum(int numThreads);

    // 如果没有监听，就启动服务器(监听)。多次调用没有副作用，线程安全。
    // TcpServer 启动 Tcp 服务器，主要是线程池的启动，mainLoop 跑 Acceptor 监听 Tcp 连接请求。线程池需要指定其初始数量，当然，这需要在 start() 之前调用 TcpServer::setThreadNum() 设置。
    void start();


private:

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;


    // 用于构造函数中，首先判断传入的 loop 是否有效，无效直接终止程序。
    static EventLoop *CheckLoopNotNull(EventLoop *loop);


    // 有一个新用户连接，acceptor 会执行这个回调操作，负责将 mainLoop 接收到的请求连接（acceptChannel 会有读事件发生），通过回调轮询分发给 subLoop 去处理。
    void newConnection(int sockfd, const InetAddress &peerAddr);

    void removeConnection(const TcpConnectionPtr &conn);

    void removeConnectionInLoop(const TcpConnectionPtr &conn);


    // 主线程 的 loop。
    EventLoop *m_mainLoop = nullptr;

    // one loop per thread。
    // shared_ptr 表示共享所有权，多个 shared_ptr 可以同时指向同一个对象。它通过控制块中的引用计数记录当前持有者数量，当最后一个 shared_ptr 被销毁时，引用计数归零，对象才会被释放。shared_ptr 适用于对象生命周期无法由单一所有者确定的场景，例如多个模块、多个回调共同持有同一个连接对象。但由于需要维护引用计数，会带来额外的内存和性能开销，并且需要注意循环引用导致的资源泄漏问题。
    std::shared_ptr<EventLoopThreadPool> m_threadPool;

    const std::string m_ipPort;

    const std::string m_name;

    // 运行在 mainLoop，任务就是监听新连接事件。
    // unique_ptr 表示独占所有权，一个对象只能由一个 unique_ptr 管理。它禁止拷贝，只允许移动，因此能够明确表示资源的唯一拥有关系，同时没有引用计数和控制块的额外开销，性能接近裸指针。通常优先使用 unique_ptr 表达明确的所有权关系，只有当对象确实需要被多个地方共同管理时，才使用 shared_ptr。
    std::unique_ptr<Acceptor> m_acceptor;

    // 有新连接时的回调。
    ConnectionCallback m_connectionCallback;

    // 有读写事件发生时的回调。
    MessageCallback m_messageCallback;

    // 消息发送完成后的回调。
    WriteCompleteCallback m_writeCompleteCallback;

    // loop 线程初始化的回调。
    ThreadInitCallback m_threadInitCallback;

    // 线程池中线程的数量。
    int m_numThreads = 0;

    std::atomic_int m_started = 0;

    int m_nextConnId = 1;

    // 保存所有的连接。
    ConnectionMap m_connections;
};


#endif

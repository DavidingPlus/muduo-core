#ifndef _MUDUO_CORE_CHANNEL_H_
#define _MUDUO_CORE_CHANNEL_H_

#include "globalmacros.h"

#include <functional>
#include <memory>

#include <sys/epoll.h>

class Timestamp;
class EventLoop;


// 在 TCP 网络编程中，想要 IO 多路复用监听某个文件描述符，就要把这个 fd 和该 fd 感兴趣的事件通过 epollctl 注册到 IO 多路复用模块（我管它叫事件监听器）上。当事件监听器监听到该 fd 发生了某个事件。事件监听器返回 [发生事件的 fd 集合] 以及 [每个 fd 都发生了什么事件]。
// Channel 类则封装了一个 [fd] 和这个 [fd 感兴趣事件] 以及事件监听器监听到 [该 fd 实际发生的事件]。同时 Channel 类还提供了设置该 fd 的感兴趣事件，以及将该 fd 及其感兴趣事件注册到事件监听器或从事件监听器上移除，以及保存了该 fd 的每种事件对应的处理函数。Channel 类其实相当于一个文件描述符的保姆！
// 理清楚 EventLoop、Channel、Poller 之间的关系。Reactor 模型上对应多路事件分发器。Channel 理解为通道，封装了 sockfd 和其感兴趣的 event，如 EPOLLIN、EPOLLOUT 事件，还绑定了 poller 返回的具体事件。
class Channel
{

    CLASS_NONCOPYABLE(Channel)

public:

    using EventCallback = std::function<void()>;

    using ReadEventCallback = std::function<void(const Timestamp &)>;


    Channel(EventLoop *loop, int fd) : m_loop(loop), m_fd(fd), m_events(0), m_revents(0), m_index(-1), m_tied(false) {}

    ~Channel() = default;

    // fd 得到 Poller 通知以后，处理事件 handleEvent 在 EventLoop::loop() 中调用。
    // 当调用 epoll_wait() 后，可以得知事件监听器上哪些 Channel（文件描述符）发生了哪些事件，事件发生后自然就要调用这些 Channel 对应的处理函数。Channel::HandleEvent() 让每个发生了事件的 Channel 调用自己保管的事件处理函数。每个 Channel 会根据自己文件描述符实际发生的事件（通过 Channel 中的 revents 变量得知）和感兴趣的事件（通过 Channel 中的 events 变量得知）来选择调用 m_readCallback 和/或 m_writeCallback 和/或 m_closeCallback 和/或 m_errorCallback。
    void handleEvent(const Timestamp &receiveTime);

    // 设置回调函数对象。一个文件描述符会发生可读、可写、关闭、错误事件。当发生这些事件后，就需要调用相应的处理函数来处理。外部通过调用上面这四个函数可以将事件处理函数放进 Channel 类中，当需要调用的时候就可以直接拿出来调用了。
    void setReadCallback(ReadEventCallback cb) { m_readCallback = std::move(cb); }

    void setWriteCallback(EventCallback cb) { m_writeCallback = std::move(cb); }

    void setCloseCallback(EventCallback cb) { m_closeCallback = std::move(cb); }

    void setErrorCallback(EventCallback cb) { m_errorCallback = std::move(cb); }

    // Channel 的 tie() 方法什么时候调用过？TcpConnection -> channel。Channel 作为 TcpConnection 的成员，总是随 TcpConnection 的销毁而销毁。因此，Channel 不会在 TcpConnection 之后存在，即 Channel 与 TcpConnection 存在生命周期依赖关系。Channel 保存 TcpConnection 的回调函数，如果 TcpConnection 已经销毁，但 epoll 仍然返回该 Channel 的事件，则执行回调会访问悬空对象。我们需要保证 Channel 执行事件回调期间，TcpConnection 生命周期不会结束。
    // tie() 提供线程安全保护，防止 EventLoop 在 TcpConnection 已销毁后调用 Channel 的回调函数。具体而言，使用 weak_ptr 关联 TcpConnection，只观察 TcpConnection 是否还存在，在事件处理前通过 lock() 临时提升为 shared_ptr，保证执行回调期间 TcpConnection 对象一定存在。使用 weak_ptr 而不是 shared_ptr，是为了避免 Channel 和 TcpConnection 之间形成循环引用。
    void tie(const std::shared_ptr<void> &obj);

    int fd() const { return m_fd; }

    int events() const { return m_events; }

    void setRevents(int revt) { m_revents = revt; }

    // 设置 fd 相应的事件状态，相当于 epoll_ctl add delete。
    // 外部通过这几个函数来告知 Channel 你所监管的文件描述符都对哪些事件类型感兴趣，并把这个文件描述符及其感兴趣事件注册到事件监听器（IO 多路复用模块）上。这些函数里面都有一个 update() 私有成员方法，这个 update 其实本质上就是调用了 epoll_ctl()，真正注册到 epoll 事件中。
    void enableReading() { m_events |= kReadEvent, update(); }

    void disableReading() { m_events &= ~kReadEvent, update(); }

    void enableWriting() { m_events |= kWriteEvent, update(); }

    void disableWriting() { m_events &= ~kWriteEvent, update(); }

    void disableAll() { m_events = kNoneEvent, update(); }

    // 返回 fd 当前的事件状态。
    bool isNoneEvent() const { return m_events == kNoneEvent; }

    bool isWriting() const { return m_events & kWriteEvent; }

    bool isReading() const { return m_events & kReadEvent; }

    int index() { return m_index; }

    void setIndex(int idx) { m_index = idx; }

    // 一个线程一个事件循环。
    EventLoop *ownerLoop() { return m_loop; }

    void remove();


private:

    void update();

    void handleEventWithGuard(const Timestamp &receiveTime);


    // static const：只是声明静态常量成员，真正的内存空间需要在 cpp 文件中定义。初始化发生在程序运行阶段。如果需要获取该变量的地址，必须保证 cpp 中存在定义。
    // static constexpr：constexpr 表示该变量是编译期常量，必须在声明时完成初始化。编译器可以直接将它替换为对应的数值，不需要额外的内存空间。不需要在 cpp 文件中再次定义。适合表示不会改变的标志位、枚举值、配置常量等。

    // 空事件。
    static constexpr int kNoneEvent = 0;

    // 读事件。
    // EPOLLPRI：表示有紧急数据（Out-Of-Band Data，带外数据）可读。主要用于 TCP 的带外数据通知，例如接收到设置了 URG 标志的数据。普通 TCP 数据到达不会触发该事件，普通读事件使用 EPOLLIN。一般网络服务器很少使用。
    static constexpr int kReadEvent = EPOLLIN | EPOLLPRI;

    // 写事件。
    static constexpr int kWriteEvent = EPOLLOUT;


    // 事件循环。
    EventLoop *m_loop;

    // fd，Poller 监听的对象。
    const int m_fd;

    // 注册 fd 感兴趣的事件。
    int m_events;

    // Poller 事件监听器实际监听到该 fd 发生的事件类型集合，当事件监听器监听到一个 fd 发生了什么事件，通过 setRevents() 函数来设置 revents 值。
    int m_revents;

    int m_index;

    std::weak_ptr<void> m_tie;

    bool m_tied;

    // 因为 channel 通道里可获知 fd 最终发生的具体的事件 events，所以它负责调用具体事件的回调操作。
    ReadEventCallback m_readCallback;

    EventCallback m_writeCallback;

    EventCallback m_closeCallback;

    EventCallback m_errorCallback;
};


#endif

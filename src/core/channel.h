#ifndef _MUDUO_CORE_CHANNEL_H_
#define _MUDUO_CORE_CHANNEL_H_

#include "globalmacros.h"

#include <functional>
#include <memory>

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
    using ReadEventCallback = std::function<void(Timestamp)>;


private:


    // 事件循环。
    EventLoop *m_loop;

    // fd，Poller 监听的对象。
    const int m_fd;

    // 注册 fd 感兴趣的事件。
    int m_events;

    // Poller 事件监听器实际监听到该 fd 发生的事件类型集合，当事件监听器监听到一个 fd 发生了什么事件，通过 set_revents() 函数来设置 revents 值。
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

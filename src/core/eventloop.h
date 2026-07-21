#ifndef _MUDUO_CORE_EVENTLOOP_H_
#define _MUDUO_CORE_EVENTLOOP_H_

#include "globalmacros.h"

#include "currentthread.h"
#include "timestamp.h"

#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

class Poller;
class Channel;


// Poller 是封装了和事件监听有关的方法和成员，调用一次 Poller::poll() 它就能给你返回事件监听器的监听结果（发生事件的 fd 及其发生的事件）。作为一个网络服务器，需要有持续监听、持续获取监听结果、持续处理监听结果对应的事件的能力，也就是我们需要循环的去调用 Poller::poll() 方法获取实际发生事件的 Channel 集合，然后调用这些 Channel 里面保管的不同类型事件的处理函数（调用 Channel::HandlerEvent() 方法）。
// EventLoop 就是负责实现“循环”，负责驱动“循环”的重要模块。Channel 和 Poller 相当于 EventLoop 的手下，EventLoop 整合封装了二者并向上提供了更方便的接口来使用。
class EventLoop
{

    CLASS_NONCOPYABLE(EventLoop)

public:

    using Functor = std::function<void()>;


    EventLoop();

    ~EventLoop();

    // 开启事件循环。
    void loop();

    // 退出事件循环。
    void quit();

    Timestamp pollReturnTime() const { return m_pollReturnTime; }

    // 在当前 loop 中执行。
    void runInLoop(Functor cb);

    // 把上层注册的回调函数 cb 放入队列中，唤醒 loop 所在的线程执行 cb。
    void queueInLoop(Functor cb);

    // 通过 eventfd 唤醒 loop 所在的线程。
    void wakeup();

    // EventLoop 的方法，内部是在调用 Poller 的方法。
    void updateChannel(Channel *channel);

    void removeChannel(Channel *channel);

    bool hasChannel(Channel *channel);

    // 判断 EventLoop 对象是否在自己的线程里。
    bool isInLoopThread() const { return CurrentThread::tid() == m_threadId; }


private:

    // 给 eventfd 返回的文件描述符 m_wakeupFd 绑定的事件回调。当 wakeup() 时，即有事件发生时调用 handleRead() 读 m_wakeupFd 的 8 字节，同时唤醒阻塞的 epoll_wait。
    void handleRead();

    // 执行上层回调。
    void doPendingFunctors();


    using ChannelList = std::vector<Channel *>;

    // 原子操作，底层通过 CAS 实现。
    std::atomic_bool m_looping;

    // 标识退出 loop 循环。
    std::atomic_bool m_quit;

    // 记录当前 EventLoop 是被哪个线程 id 创建的，即标识了当前 EventLoop 的所属线程 id。
    const pid_t m_threadId = CurrentThread::tid();

    // Poller 返回发生事件的 Channels 的时间点。
    Timestamp m_pollReturnTime;

    std::unique_ptr<Poller> m_poller;

    // 作用：当 mainLoop 获取一个新用户的 Channel 需通过轮询算法选择一个 subLoop 通过该成员唤醒 subLoop 处理 Channel。
    int m_wakeupFd;

    std::unique_ptr<Channel> m_wakeupChannel;

    // 返回 Poller 检测到当前有事件发生的所有 Channel 列表。
    ChannelList m_activeChannels;

    // 标识当前 loop 是否有需要执行的回调操作。
    std::atomic_bool m_callingPendingFunctors;

    // 存储 loop 需要执行的所有回调操作。
    std::vector<Functor> m_pendingFunctors;

    // 互斥锁，用来保护上面 vector 容器的线程安全操作。
    std::mutex m_mutex;
};


#endif

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
// EventLoop 就是负责实现“循环”，负责驱动“循环”的重要模块。Channel 和 Poller 相当于 EventLoop 的手下，EventLoop 整合封装了二者并向上提供了更方便的接口来使用。每一个 EventLoop 都绑定了一个线程（一对一绑定），这种运行模式是 Muduo 库的特色！！充份利用了多核 cpu 的能力，每一个核的线程负责循环监听一组文件描述符的集合。
// 总的来说，EventLoop 起到一个驱动循环的功能，Poller 负责从事件监听器上获取监听结果。而 Channel 类则在其中起到了将 fd 及其相关属性封装的作用，将 fd 及其感兴趣事件和发生的事件以及不同事件对应的回调函数封装在一起，这样在各个模块中传递更加方便。接着 EventLoop 调用 EventLoop::loop() 开启事件循环。
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

    // 在当前 EventLoop 所属线程执行 cb。若调用线程就是该 EventLoop 线程，直接执行；否则调用 queueInLoop() 将 cb 投递到该 EventLoop 的任务队列，由其线程执行。
    // 例如：线程 A/B 调用 B->runInLoop(cb) 请求线程 B 执行 cb。如果调用者线程（A/B）就是该 EventLoop 所属线程（线程 B），则直接执行 cb，避免不必要的线程切换。如果调用者线程不是该 EventLoop 所属线程（线程 A），则通过 queueInLoop() 将 cb 投递给 EventLoop B，最终由线程 B 执行 cb。
    void runInLoop(Functor cb);

    // 将任务加入当前 EventLoop 的 pendingFunctors 队列，由该 EventLoop 所属线程在事件循环中调用 doPendingFunctors() 执行。和 runInLoop() 不同，前者若调用线程就是 EventLoop 所属线程，则不用加入事件循环，直接执行，提升效率，queueInLoop() 会统一加入事件循环中执行。
    // 例如：线程 A 调用 loopB->queueInLoop(cb)，cb 会加入 EventLoop B 的任务队列，最终由线程 B 执行。
    void queueInLoop(Functor cb);

    // 通过 eventfd 唤醒 loop 所在的线程。向 m_wakeupFd 写一个数据，wakeupChannel 就发生读事件，当前 loop 线程就会被唤醒。
    void wakeup();

    // EventLoop 的方法，内部是在调用 Poller 的方法。
    void updateChannel(Channel *channel);

    void removeChannel(Channel *channel);

    bool hasChannel(Channel *channel);

    // 判断 EventLoop 对象是否在自己的线程里。
    bool isInLoopThread() const { return CurrentThread::tid() == m_threadId; }


private:

    /* 创建线程之后主线程和子线程谁先运行是不确定的。通过一个 eventfd 在线程之间传递数据的好处是多个线程无需上锁就可以实现同步。eventfd 支持的最低内核版本为 Linux 2.6.27，在 2.6.26 及之前的版本也可以使用 eventfd，但是 flags 必须设置为 0。
     * 函数原型：
     *     #include <sys/eventfd.h>
     *     int eventfd(unsigned int initval, int flags);
     * 参数说明：
     *      initval：初始化计数器的值。
     *      flags：EFD_NONBLOCK,设置 socket 为非阻塞。EFD_CLOEXEC，执行 fork 的时候，在父进程中的描述符会自动关闭，子进程中的描述符保留。
     * 场景：
     *     eventfd 可以用于同一个进程之中的线程之间的通信。
     *     eventfd 还可以用于同亲缘关系的进程之间的通信。
     *     eventfd 用于不同亲缘关系的进程之间通信的话需要把 eventfd 放在几个进程共享的共享内存中（没有测试过）。
     */

    // 创建 wakeupfd 用来 notify 唤醒 subReactor 处理新来的 channel。
    static int createEventfd();


    // 给 eventfd 返回的文件描述符 m_wakeupFd 绑定的事件回调。当 wakeup() 时，即有事件发生时调用 handleRead() 读 m_wakeupFd 的 8 字节，同时唤醒阻塞的 epoll_wait。
    void handleRead();

    // 执行上层回调。
    void doPendingFunctors();


    using ChannelList = std::vector<Channel *>;


    // 定义默认的 Poller IO 复用接口的超时时间，10000 毫秒，即 10 秒钟。
    static constexpr int kPollTimeMs = 10000;


    // 原子操作，底层通过 CAS 实现。

    // 标识事件循环是否正在运行。
    std::atomic_bool m_looping = false;

    // 标识退出 loop 循环。
    std::atomic_bool m_quit = false;

    // 记录当前 EventLoop 是被哪个线程 id 创建的，即标识了当前 EventLoop 的所属线程 id。
    const pid_t m_threadId = CurrentThread::tid();

    // Poller 返回发生事件的 Channels 的时间点。
    Timestamp m_pollReturnTime;

    std::unique_ptr<Poller> m_poller;

    // 当 mainLoop 需要处理一个新 Channel 时，需通过轮询算法选择一个 subLoop，同时唤醒 subLoop 处理 Channel。
    // m_wakeupFd 就是一个专门用来唤醒 epoll_wait() 的文件描述符。
    int m_wakeupFd = -1;

    std::unique_ptr<Channel> m_wakeupChannel;

    // 返回 Poller 检测到当前有事件发生的所有 Channel 列表。
    ChannelList m_activeChannels;

    // 存储提交给当前 EventLoop 执行的回调任务，希望由当前 EventLoop 所属线程执行的任务。这些任务通常由其他线程通过 queueInLoop() 投递，当然也可以由当前线程在执行任务过程中产生。与 Poller 返回的 IO 事件不同，这些任务最终由当前 EventLoop 所属线程在 doPendingFunctors() 中执行。
    std::vector<Functor> m_pendingFunctors;

    // 标识当前 EventLoop 是否正在执行 pendingFunctors 队列中的任务。当 EventLoop 执行任务期间，又有新的任务加入队列时，需要通过 wakeup() 唤醒当前 EventLoop，避免新任务等待下一次 IO 事件后才能执行。
    std::atomic_bool m_callingPendingFunctors = false;

    // 互斥锁，用来保护上面 vector 容器的线程安全操作。
    std::mutex m_mutex;
};


#endif

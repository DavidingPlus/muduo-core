#include "eventloop.h"

#include "poller.h"
#include "channel.h"
#include "logger.h"

#include <cassert>

#include <sys/eventfd.h>


// 线程局部变量，一个线程只有一个。防止一个线程创建多个 EventLoop。
static thread_local EventLoop *t_loopInThisThread = nullptr;


int EventLoop::CreateEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0) LOG_FATAL("eventfd error: {}", errno);
    return evtfd;
}

EventLoop::EventLoop()
    : m_poller(Poller::NewDefaultPoller(this)), m_wakeupFd(CreateEventfd()), m_wakeupChannel(new Channel(this, m_wakeupFd))
{
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop {} exists in this thread {}", fmt::ptr(t_loopInThisThread), m_threadId);
    }
    else
    {
        t_loopInThisThread = this;
    }

    LOG_DEBUG("EventLoop created {} in thread {}", fmt::ptr(this), m_threadId);

    // 和 pipe() 管道类似，eventfd 用于 EventLoop 线程间通信。而 m_wakeupFd 就是专门用于存储 eventfd 的文件描述符。
    // 当一个线程需要唤醒另一个 EventLoop 时，通过 write(eventfd) 写入通知。目标 EventLoop 将 eventfd 封装为 Channel，并监听 EPOLLIN 事件。eventfd 被写入后变为可读状态，epoll_wait 返回，EventLoop 被唤醒，随后通过 handleRead() 清空 eventfd，并执行 pendingFunctors 中的任务。每一个 EventLoop 都将监听 m_wakeupChannel 的 EPOLL 读事件了。
    m_wakeupChannel->enableReading();
    // 设置 wakeupFd 的事件类型以及发生事件后的回调操作。
    m_wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
}

EventLoop::~EventLoop()
{
    assert(!m_looping);

    // 给 Channel 移除所有感兴趣的事件。
    m_wakeupChannel->disableAll();
    // 把 Channel 从 EventLoop 上删除掉。
    m_wakeupChannel->remove();

    ::close(m_wakeupFd);

    t_loopInThisThread = nullptr;
}

void EventLoop::loop()
{
    // 每个 EventLoop 对象都唯一绑定了一个线程，这个线程其实就在一直执行这个函数里面的 while 循环，这个 while 循环的大致逻辑比较简单。就是调用 Poller:poll() 方法获取事件监听器上的监听结果。接下来在 loop 里面就会调用监听结果中每一个 Channel 的处理函数 HandlerEvent()。
    // 每一个 Channel 的处理函数会根据 Channel 类中封装的实际发生的事件，执行 Channel 类中封装的各事件处理函数。比如一个 Channel 发生了可读事件，可写事件，则这个 Channel 的 HandlerEvent() 就会调用提前注册在这个 Channel 的可读事件和可写事件处理函数，又比如另一个 Channel 只发生了可读事件，那么 HandlerEvent() 就只会调用提前注册在这个 Channel 中的可读事件处理函数。

    m_looping = true;
    m_quit = false;

    LOG_INFO("EventLoop {} start looping", fmt::ptr(this));

    while (!m_quit)
    {
        // 执行当前 EventLoop 事件循环需要处理的回调操作。对于线程数 >= 2 的情况，IO 线程 mainloop(mainReactor) 主要工作：

        // 1. 正常 poller 获取的 IO 事件。例如：accept 接收连接，将 accept 返回的 connfd 打包为 Channel，TcpServer::newConnection 通过轮询将 TcpConnection 对象分配给 subloop 处理。
        m_activeChannels.clear();
        m_pollReturnTime = m_poller->poll(kPollTimeMs, &m_activeChannels);

        // Poller 监听哪些 channel 发生了事件 然后上报给 EventLoop，通知 channel 处理相应的事件。
        for (Channel *channel : m_activeChannels) channel->handleEvent(m_pollReturnTime);

        // 2. 存储提交给当前 EventLoop 执行的回调任务 m_pendingFunctors。例如：mainloop 调用 queueInLoop 将回调加入 subloop（该回调需要 subloop 执行，但 subloop 还在 m_poller->poll() 处阻塞），queueInLoop 通过 wakeup() 将 subloop 唤醒。
        doPendingFunctors();
    }

    LOG_INFO("EventLoop {} stop looping", fmt::ptr(this));

    m_looping = false;
}

void EventLoop::quit()
{
    // 退出事件循环。
    // 1. 如果 loop 在自己的线程中调用 quit 成功了，说明当前线程已经执行完毕了 loop() 函数的 m_poller->poll 并退出。
    // 2. 如果不是当前 EventLoop 所属线程中调用 quit 退出 EventLoop，需要唤醒 EventLoop 所属线程的 epoll_wait。比如在一个 subloop(worker) 中调用 mainloop(IO) 的 quit 时，需要唤醒 mainloop(IO) 的 m_poller->poll 让其执行完 loop() 函数。
    // 注意：正常情况下 mainloop 负责请求连接，将回调写入 subloop 中，通过生产者消费者模型即可实现线程安全的队列。但是 muduo 通过 wakeup() 机制，使用 eventfd 创建的 m_wakeupFd 进行唤醒，使得 mainloop 和 subloop 之间能够进行通信。

    m_quit = true;

    if (!isInLoopThread()) wakeup();
}

void EventLoop::runInLoop(EventLoop::Functor cb)
{
    // 如果当前线程就是 EventLoop 所属线程，则直接执行回调。此时会在当前线程的调用栈中同步执行，天然不会和事件循环的其他流程冲突，因为同一个线程同一时刻只能执行一个函数。
    if (isInLoopThread())
    {
        cb();
    }
    // 在非当前 EventLoop 线程中执行 cb，就需要唤醒 EventLoop 所在线程执行 cb。
    else
    {
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(EventLoop::Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_pendingFunctors.emplace_back(cb);
    }

    // !isInLoopThread()：表示当前调用 queueInLoop() 的线程不是目标 EventLoop 所属线程。例如：线程 A 调用 loopB->queueInLoop(cb)，希望线程 B 执行 cb。此时线程 B 可能阻塞在 poll() 中等待 IO 事件，需要通过 wakeup() 写入 eventfd 唤醒线程 B，使其及时执行 m_pendingFunctors 中的任务。另外，如果线程 B 当前正在处理 IO 事件，cb 会先进入 m_pendingFunctors 队列，等当前 IO 事件处理完成后，在 doPendingFunctors() 中执行，语义依旧不会被破坏。
    // m_callingPendingFunctors：表示当前 EventLoop 正在执行 m_pendingFunctors 中的任务。如果执行任务过程中又调用 queueInLoop() 添加了新的任务，新任务不会出现在当前正在执行的任务列表中，需要等待下一次 doPendingFunctors() 执行。因此通过 wakeup() 唤醒 EventLoop，避免新任务等待下一次 IO 事件。
    if (!isInLoopThread() || m_callingPendingFunctors) wakeup();
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(m_wakeupFd, &one, sizeof(one));
    if (sizeof(one) != n) LOG_ERROR("EventLoop::wakeup() writes {} bytes instead of 8", n);
}

void EventLoop::updateChannel(Channel *channel)
{
    m_poller->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    m_poller->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return m_poller->hasChannel(channel);
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = ::read(m_wakeupFd, &one, sizeof(one));
    if (sizeof(one) != n) LOG_ERROR("EventLoop::handleRead() reads {} bytes instead of 8", n);
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    m_callingPendingFunctors = true;

    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // 将待执行任务交换到局部变量。
        // 1. 避免死锁：functor() 中可能再次调用 queueInLoop() 提交新任务。如果此时仍然持有 m_mutex，queueInLoop() 会再次尝试获取同一把 mutex，而 std::mutex 不支持递归加锁，会导致当前线程等待自己释放锁，形成死锁。
        // 2. 减少锁的占用时间，提高并发效率：mutex 只保护 m_pendingFunctors 的交换操作，不保护后续 functor() 的执行过程。避免执行耗时任务时长期占用锁，阻塞其他线程调用 queueInLoop()。
        // 3. 保证新加入任务不会影响当前执行批次：当前批次任务交换到 functors 后，如果执行过程中又有新的任务通过 queueInLoop() 加入，新任务会进入新的 m_pendingFunctors 队列，等下一轮 doPendingFunctors() 再执行。同时通过交换所有权，把当前待执行任务整体搬走，让原队列 m_pendingFunctors 自然变为空，不影响后续语义。
        functors.swap(m_pendingFunctors);
    }

    // 执行当前 loop 需要执行的回调操作。
    for (auto &functor : functors) functor();

    m_callingPendingFunctors = false;
}

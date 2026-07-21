#include "eventloop.h"

#include "poller.h"
#include "channel.h"
#include "logger.h"

#include <cassert>

#include <sys/eventfd.h>


// 线程局部变量，一个线程只有一个。防止一个线程创建多个 EventLoop。
static thread_local EventLoop *t_loopInThisThread = nullptr;


int EventLoop::createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0) LOG_FATAL("eventfd error: {}", errno);
    return evtfd;
}

EventLoop::EventLoop()
    : m_poller(Poller::newDefaultPoller(this)), m_wakeupFd(createEventfd()), m_wakeupChannel(new Channel(this, m_wakeupFd))
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
    m_looping = true;
    m_quit = false;

    LOG_INFO("EventLoop {} start looping", fmt::ptr(this));

    while (!m_quit)
    {
        m_activeChannels.clear();
        m_pollReturnTime = m_poller->poll(kPollTimeMs, &m_activeChannels);

        // Poller 监听哪些 channel 发生了事件 然后上报给 EventLoop，通知 channel 处理相应的事件。
        for (Channel *channel : m_activeChannels) channel->handleEvent(m_pollReturnTime);

        // 执行当前 EventLoop 事件循环需要处理的回调操作。对于线程数 >=2 的情况，IO 线程 mainloop(mainReactor) 主要工作：
        // 1. accept 接收连接，将 accept 返回的 connfd 打包为 Channel，TcpServer::newConnection 通过轮询将 TcpConnection 对象分配给 subloop 处理。
        // 2. mainloop 调用 queueInLoop 将回调加入 subloop（该回调需要 subloop 执行，但 subloop 还在 m_poller->poll() 处阻塞），queueInLoop 通过 wakeup() 将 subloop 唤醒。
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
    // 当前 EventLoop 中执行回调。
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
}

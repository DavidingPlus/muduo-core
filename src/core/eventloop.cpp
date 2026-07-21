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
}

void EventLoop::quit()
{
}

void EventLoop::runInLoop(EventLoop::Functor cb)
{
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

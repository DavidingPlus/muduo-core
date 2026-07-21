#include "epollpoller.h"

#include "logger.h"
#include "timestamp.h"


EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop), m_epollfd(::epoll_create1(EPOLL_CLOEXEC)), m_events(EventList(kInitEventListSize))
{
    if (m_epollfd < 0) LOG_FATAL("epoll_create error: {}", errno);
}

// TODO
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    /**
     * epoll的使用：
     * 1. epoll_create
     * 2. epoll_ctl (add, mod, del)
     * 3. epoll_wait
     **/
}

void EPollPoller::updateChannel(Channel *channel)
{
}

void EPollPoller::removeChannel(Channel *channel)
{
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
}

void EPollPoller::update(int operation, Channel *channel)
{
}

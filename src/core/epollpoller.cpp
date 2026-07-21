#include "epollpoller.h"

#include "logger.h"


EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop), m_epollfd(::epoll_create1(EPOLL_CLOEXEC)), m_events(EventList(kInitEventListSize))
{
    if (m_epollfd < 0) LOG_FATAL("epoll_create error: {}", errno);
}

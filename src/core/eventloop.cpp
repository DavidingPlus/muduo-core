#include "eventloop.h"

#include "poller.h"


EventLoop::EventLoop()
{
}

EventLoop::~EventLoop()
{
}

void EventLoop::updateChannel(Channel *channel)
{
}

void EventLoop::removeChannel(Channel *channel)
{
}

bool EventLoop::hasChannel(Channel *channel)
{
    return m_poller->hasChannel(channel);
}

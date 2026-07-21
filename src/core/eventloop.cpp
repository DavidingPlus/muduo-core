#include "eventloop.h"

#include "poller.h"


EventLoop::EventLoop()
{
}

EventLoop::~EventLoop()
{
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
}

void EventLoop::doPendingFunctors()
{
}

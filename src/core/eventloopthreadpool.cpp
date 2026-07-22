#include "eventloopthreadpool.h"

#include "eventloop.h"
#include "eventloopthread.h"


EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
}

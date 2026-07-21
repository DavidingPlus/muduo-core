#ifndef _MUDUO_CORE_EVENTLOOP_H_
#define _MUDUO_CORE_EVENTLOOP_H_

#include "globalmacros.h"

class Channel;


class EventLoop
{
    CLASS_NONCOPYABLE(EventLoop)

public:

    EventLoop();

    ~EventLoop();

    // EventLoop 的方法，内部是在调用 Poller 的方法。
    void updateChannel(Channel *channel);

    void removeChannel(Channel *channel);

    bool hasChannel(Channel *channel);

private:
};


#endif

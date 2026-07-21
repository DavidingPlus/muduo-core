#include "poller.h"

#include <string>
#include <cstdlib>


Poller *Poller::newDefaultPoller(EventLoop *loop)
{
    const char *poller = ::getenv("MUDUO_DEFAULT_POLLER");

    // TODO 暂不支持 Poll。
    if (poller && std::string("Poll") == poller)
    {
        return nullptr; // 生成 poll 的实例。
    }
    // 默认使用 Epoll。
    else
    {
        // return new EPollPoller(loop); // 生成 epoll 的实例。
    }
}

#include "poller.h"

#include <cstdlib>


// Poller *Poller::newDefaultPoller(EventLoop *loop)
// {
//     if (::getenv("MUDUO_USE_EPOLL"))
//     {
//         return new EPollPoller(loop); // 生成 epoll 的实例。
//     }
//     // TODO 暂不支持 Poll。
//     else
//     {
//         return nullptr; // 生成 poll 的实例。
//     }
// }

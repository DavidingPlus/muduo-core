#include "netutils.h"

#include "logger.h"

#include <cerrno>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/eventfd.h>
#include <sys/socket.h>


namespace NetUtils
{

    int CreateEventfd()
    {
        int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (evtfd < 0) LOG_FATAL("eventfd error: {}", errno);
        return evtfd;
    }

    int CreateSocketNonblocking()
    {
        int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
        if (sockfd < 0) LOG_FATAL("{}:{}:{} listen socket create err: {}", __FILE__, __FUNCTION__, __LINE__, errno);
        return sockfd;
    }

    EventLoop *CheckLoopNotNull(EventLoop *loop)
    {
        if (!loop) LOG_FATAL("{}:{}:{} mainLoop is null!", __FILE__, __FUNCTION__, __LINE__);
        return loop;
    }

}

#include "epollpoller.h"

#include "channel.h"
#include "logger.h"
#include "timestamp.h"


EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop), m_epollfd(::epoll_create1(EPOLL_CLOEXEC)), m_events(EventList(kInitEventListSize))
{
    if (m_epollfd < 0) LOG_FATAL("epoll_create error: {}", errno);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    /**
     * epoll的使用：
     * 1. epoll_create
     * 2. epoll_ctl (add, mod, del)
     * 3. epoll_wait
     **/

    // 由于频繁调用 poll，用 LOG_DEBUG 输出日志更好一点更为合理。当遇到并发场景，关闭 DEBUG 日志提升效率。
    LOG_DEBUG("func={} -> fd total count: {}", __FUNCTION__, m_channels.size());

    int saveErrno = errno;
    Timestamp now(Timestamp::now());

    // epoll_wait 会阻塞等待已注册的文件描述符事件发生。m_events 是用户空间保存 epoll 返回事件的缓冲区。每次调用 epoll_wait 时，内核都会重新向该数组写入当前发生的事件，并不会追加上一次的数据。返回值 numEvents 表示本次有效事件数量，因此只需要处理 m_events[0, numEvents - 1]。如果 numEvents 等于 m_events 当前容量，说明用户提供的事件数组可能被填满，epoll 中可能还有更多就绪事件没有返回，内核会处理让这部分事件会在下次 epoll_wait 的时候返回。因此扩大 m_events 容量，让下一次 epoll_wait 可以接收更多事件，尝试提高效率。
    int numEvents = ::epoll_wait(m_epollfd, m_events.data(), static_cast<int>(m_events.size()), timeoutMs);
    if (numEvents > 0)
    {
        LOG_DEBUG("%d events happend\n", numEvents);

        // 把监听到的 Channel 装进 activeChannels 中（它是一个 vector<Channel*>）。这样，当外界调用完 poll 之后就能拿到事件监听器的监听结果。（activeChannels）。
        fillActiveChannels(numEvents, activeChannels);

        // 扩容操作。
        if (m_events.size() == numEvents) m_events.resize(m_events.size() * 2);
    }
    // 返回值为 0，代表超时。
    else if (0 == numEvents)
    {
        LOG_DEBUG("%s timeout!", __FUNCTION__);
    }
    else
    {
        // epoll_wait 被信号打断（EINTR），不算真正错误，下次重试即可。其他情况就是错误。
        if (EINTR != saveErrno)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() error!");
        }
    }


    return now;
}

void EPollPoller::updateChannel(Channel *channel)
{
}

void EPollPoller::removeChannel(Channel *channel)
{
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = reinterpret_cast<Channel *>(m_events[i].data.ptr);
        channel->setRevents(m_events[i].events);
        activeChannels->emplace_back(channel);

        // 这样 EventLoop 就拿到了它的 Poller 给它返回的所有发生事件的 channel 列表了。
    }
}

void EPollPoller::update(int operation, Channel *channel)
{
    int fd = channel->fd();

    epoll_event event{};

    event.events = channel->events();
    // epoll_data 是联合体所以 data.fd 写了没有意义会被 data.ptr 覆盖。
    // typedef union epoll_data
    // {
    //     void *ptr;
    //     int fd;
    //     uint32_t u32;
    //     uint64_t u64;
    // } epoll_data_t;
    event.data.ptr = channel;

    if (::epoll_ctl(m_epollfd, operation, fd, &event) < 0)
    {
        if (EPOLL_CTL_ADD == operation)
        {
            LOG_FATAL("epoll_ctl add error: {}", errno);
        }
        else if (EPOLL_CTL_MOD == operation)
        {
            LOG_FATAL("epoll_ctl mod error: {}", errno);
        }
        else
        {
            LOG_ERROR("epoll_ctl del error: {}", errno);
        }
    }
}

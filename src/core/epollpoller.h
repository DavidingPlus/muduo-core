#ifndef _MUDUO_CORE_EPOLLER_H_
#define _MUDUO_CORE_EPOLLER_H_

#include "poller.h"

#include <vector>

#include <unistd.h>
#include <sys/epoll.h>

class EventLoop;
class Channel;


class EPollPoller : public Poller
{

public:

    EPollPoller(EventLoop *loop);

    ~EPollPoller() override { ::close(m_epollfd); }

    // 重写基类 Poller 的抽象方法。

    // 当外部调用 poll 方法的时候，该方法底层其实是通过 epoll_wait 获取这个事件监听器上发生事件的 fd 及其对应发生的事件。我们知道每个 fd 都是由一个 Channel 封装的，通过哈希表 m_channels 可以根据 fd 找到封装这个 fd 的 Channel。将事件监听器监听到该 fd 发生的事件写进这个 Channel 中的 revents 成员变量中。然后把这个 Channel 装进 activeChannels 中（它是一个 vector<Channel*>）。这样，当外界调用完 poll 之后就能拿到事件监听器的监听结果（activeChannels）。这个  activeChannels 就是事件监听器监听到的发生事件的 fd，以及每个 fd 都发生了什么事件。
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;

    void updateChannel(Channel *channel) override;

    void removeChannel(Channel *channel) override;


private:

    using EventList = std::vector<epoll_event>;


    static constexpr int kInitEventListSize = 16;


    // 将 epoll 返回的就绪事件转换成 Channel 列表。epoll_wait 返回的是 epoll_event 数组，其中保存了发生事件的文件描述符信息。在注册 epoll 事件时，通过 event.data.ptr 保存了对应的 Channel 指针，所以这里可以直接取出 Channel 对象，无需再通过 fd 查找。同时将实际发生的事件保存到 Channel 的 m_revents 成员中，供 EventLoop 后续处理。Channel 列表通过入参指针 activeChannels 传出。
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

    // 更新 channel 通道，其实就是调用 epoll_ctl。
    void update(int operation, Channel *channel);


    // epoll_create 创建返回的 fd 保存在 m_epollfd 中。
    int m_epollfd = -1;

    // 用于存放 epoll_wait 返回的所有发生的事件的文件描述符事件集。
    EventList m_events = EventList(kInitEventListSize);
};


#endif

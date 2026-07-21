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

    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;

    void updateChannel(Channel *channel) override;

    void removeChannel(Channel *channel) override;


private:

    using EventList = std::vector<epoll_event>;


    static constexpr int kInitEventListSize = 16;


    // 填写活跃的连接。
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

    // 更新 channel 通道，其实就是调用 epoll_ctl。
    void update(int operation, Channel *channel);


    // epoll_create 创建返回的 fd 保存在 m_epollfd 中。
    int m_epollfd;

    // 用于存放 epoll_wait 返回的所有发生的事件的文件描述符事件集。
    EventList m_events;
};


#endif

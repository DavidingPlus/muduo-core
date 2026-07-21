#ifndef _MUDUO_CORE_POLLER_H_
#define _MUDUO_CORE_POLLER_H_

#include "globalmacros.h"

#include <vector>
#include <unordered_map>

class EventLoop;
class Timestamp;
class Channel;


// 负责监听文件描述符事件是否触发以及返回发生事件的文件描述符以及具体事件的模块就是 Poller。所以一个 Poller 对象对应一个事件监听器。在 multi-reactor 模型中，有多少 reactor 就有多少 Poller。
// 这个 Poller 是个抽象虚类，由 EpollPoller 和 PollPoller 继承实现，与监听文件描述符和返回监听结果的具体方法也基本上是在这两个派生类中实现。EpollPoller 就是封装了用 epoll 方法实现的与事件监听有关的各种方法，PollPoller 就是封装了 poll 方法实现的与事件监听有关的各种方法。
class Poller
{
    CLASS_NONCOPYABLE(Poller)

public:

    using ChannelList = std::vector<Channel *>;


    Poller(EventLoop *loop) : m_ownerLoop(loop) {}

    virtual ~Poller() = default;

    // 给所有 IO 复用保留统一的接口。
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;

    virtual void updateChannel(Channel *channel) = 0;

    virtual void removeChannel(Channel *channel) = 0;

    // 判断参数 channel 是否在当前的 Poller 当中。
    bool hasChannel(Channel *channel) const;

    // EventLoop 可以通过该接口获取默认的 IO 复用的具体实现。
    static Poller *newDefaultPoller(EventLoop *loop);


protected:

    // 哈希表，映射关系 [fd, Channel *]。
    using ChannelMap = std::unordered_map<int, Channel *>;


    ChannelMap m_channels;


private:

    // 定义 Poller 所属的事件循环 EventLoop。
    EventLoop *m_ownerLoop;
};


#endif

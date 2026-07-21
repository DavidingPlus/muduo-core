#ifndef _MUDUO_CORE_POLLER_H_
#define _MUDUO_CORE_POLLER_H_

#include "globalmacros.h"

#include <vector>
#include <unordered_map>

class EventLoop;
class Timestamp;
class Channel;


// 负责监听文件描述符事件是否触发以及返回发生事件的文件描述符以及具体事件的模块就是 Poller。所以一个 Poller 对象对应一个事件监听器。在 multi-reactor 模型中，有多少 reactor 就有多少 Poller。这个 Poller 是个抽象虚类，由 EpollPoller 和 PollPoller 继承实现，与监听文件描述符和返回监听结果的具体方法也基本上是在这两个派生类中实现。
// EpollPoller 就是封装了用 epoll 方法实现的与事件监听有关的各种方法，PollPoller 就是封装了 poll 方法实现的与事件监听有关的各种方法（本项目中暂不实现）。
// Poller 是 Reactor 模型中的 IO 多路复用封装层。Poller 本身不关心具体使用 epoll 还是 poll，只定义统一接口，具体实现由 EpollPoller / PollPoller 完成。负责:
// 1. 监听注册到系统中的文件描述符事件。
// 2. 获取发生事件的文件描述符。
// 3. 将发生事件的 Channel 返回给 EventLoop 处理。
class Poller
{

    CLASS_NONCOPYABLE(Poller)

public:

    using ChannelList = std::vector<Channel *>;


    Poller(EventLoop *loop) : m_ownerLoop(loop) {}

    virtual ~Poller() = default;

    // 给所有 IO 复用保留统一的接口。

    // 等待 IO 事件发生。timeoutMs：等待超时时间。activeChannels：返回当前发生事件的 Channel。
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;

    virtual void updateChannel(Channel *channel) = 0;

    virtual void removeChannel(Channel *channel) = 0;

    // 判断参数 channel 是否在当前的 Poller 当中。
    bool hasChannel(Channel *channel) const;


    // EventLoop 可以通过该接口获取默认的 IO 复用的具体实现。
    static Poller *newDefaultPoller(EventLoop *loop);


protected:

    // 哈希表，映射关系 [fd, Channel *]。
    // 分清两条线：
    // 1. Channel 中保存 fd，是为了注册监听时通过 Channel 获取 fd。Channel -> fd
    // 2. Poller 保存这个映射，是为了事件触发后，由于 epoll_wait 返回的只有 fd，需要通过该表快速找到对应的 Channel。fd -> Channel
    using ChannelMap = std::unordered_map<int, Channel *>;


    ChannelMap m_channels;


private:

    // 定义 Poller 所属的事件循环 EventLoop。
    EventLoop *m_ownerLoop = nullptr;
};


#endif

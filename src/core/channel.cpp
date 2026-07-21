#include "channel.h"

#include "timestamp.h"
#include "eventloop.h"
#include "logger.h"


void Channel::handleEvent(const Timestamp &receiveTime)
{
}

void Channel::tie(const std::shared_ptr<void> &)
{
}

// update 和 remove -> EventLoop -> EpollPoller 更新 channel 在 poller 中的状态。当改变 channel 所表示的 fd 的 events 事件后，update 负责在 poller 里面更改 fd 相应的事件 epoll_ctl。

void Channel::remove()
{
    // 在 channel 所属的 EventLoop 中把当前的 channel 删除掉。
    m_loop->removeChannel(this);
}

void Channel::update()
{
    // 通过 channel 所属的 eventloop，调用 poller 的相应方法，注册 fd 的 events 事件。
    m_loop->updateChannel(this);
}

void Channel::handleEventWithGuard(const Timestamp &receiveTime)
{
    LOG_INFO("channel handleEvent revents:%d\n", m_revents);

    // 关闭。
    // 如果 socket 通过 shutdown 关闭写端 SHUT_WR（epoll 触发 EPOLLHUP），并且没有可读数据，那么认为连接已经关闭，触发 close 回调。
    if ((m_revents & EPOLLHUP) && !(m_revents & EPOLLIN))
    {
        if (m_closeCallback) m_closeCallback();
    }
    // 错误。
    if (m_revents & EPOLLERR)
    {
        if (m_errorCallback) m_errorCallback();
    }
    // 读。
    if (m_revents & (EPOLLIN | EPOLLPRI))
    {
        if (m_readCallback) m_readCallback(receiveTime);
    }
    // 写。
    if (m_revents & EPOLLOUT)
    {
        if (m_writeCallback) m_writeCallback();
    }
}

#include "poller.h"

#include "channel.h"


bool Poller::hasChannel(Channel *channel) const
{
    auto it = m_channels.find(channel->fd());
    return m_channels.end() != it && it->second == channel;
}

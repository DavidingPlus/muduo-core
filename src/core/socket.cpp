#include "socket.h"


Socket::~Socket()
{
}

void Socket::bindAddress(const InetAddress &localaddr)
{
}

void Socket::listen()
{
}

int Socket::accept(InetAddress *peeraddr)
{
}

void Socket::shutdownWrite()
{
}

void Socket::setTcpNoDelay(bool on)
{
}

void Socket::setReuseAddr(bool on)
{
}

void Socket::setReusePort(bool on)
{
}

void Socket::setKeepAlive(bool on)
{
}

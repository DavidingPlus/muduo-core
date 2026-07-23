#include "tcpconnection.h"


TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr)
{
}

TcpConnection::~TcpConnection()
{
}

void TcpConnection::send(const std::string &buf)
{
}

void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count)
{
}

void TcpConnection::shutdown()
{
}

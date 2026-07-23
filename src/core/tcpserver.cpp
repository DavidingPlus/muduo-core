#include "tcpserver.h"


TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &name, Option option)
{
}

TcpServer::~TcpServer()
{
}

void TcpServer::setThreadNum(int numThreads)
{
}

void TcpServer::start()
{
}

void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
}

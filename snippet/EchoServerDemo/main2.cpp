#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "buffer.h"
#include "eventloop.h"
#include "inetaddress.h"
#include "logger.h"
#include "tcpconnection.h"
#include "tcpserver.h"


class EchoServer
{

public:

    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name, int expectedClients)
        : m_server(loop, addr, name), m_loop(loop), m_expectedClients(expectedClients)
    {
        m_server.setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        m_server.setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        m_server.setThreadNum(3);
    }

    void start() { m_server.start(); }


private:

    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            ++m_totalConnections;
            ++m_aliveConnections;
            LOG_INFO("Connection UP : {}", conn->peerAddress().toIpPort());
        }
        else
        {
            const int alive = --m_aliveConnections;
            LOG_INFO("Connection DOWN : {}", conn->peerAddress().toIpPort());
            if (m_totalConnections.load() >= m_expectedClients && 0 == alive)
            {
                m_loop->queueInLoop([this]()
                                    { m_loop->quit(); });
            }
        }
    }

    void onMessage(const TcpConnectionPtr &conn, Buffer &buf, const Timestamp &time)
    {
        (void)time;
        conn->send(buf.retrieveAllAsString());
    }


    TcpServer m_server;
    EventLoop *m_loop = nullptr;
    const int m_expectedClients = 0;
    std::atomic_int m_totalConnections = 0;
    std::atomic_int m_aliveConnections = 0;
};

namespace
{

    int connectWithRetry(uint16_t port)
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) throw std::runtime_error("socket() failed");

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        if (1 != ::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr))
        {
            ::close(fd);
            throw std::runtime_error("inet_pton() failed");
        }

        for (int attempt = 0; attempt < 40; ++attempt)
        {
            if (0 == ::connect(fd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr))) return fd;

            if (ECONNREFUSED != errno)
            {
                const std::string error = "connect() failed: " + std::string(std::strerror(errno));
                ::close(fd);
                throw std::runtime_error(error);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        const std::string error = "connect() retry timeout: " + std::string(std::strerror(errno));
        ::close(fd);
        throw std::runtime_error(error);
    }

    void runClient(int index, uint16_t port)
    {
        const int fd = connectWithRetry(port);

        const std::string message = "echo-client-" + std::to_string(index);
        const ssize_t sent = ::send(fd, message.data(), message.size(), 0);
        if (static_cast<ssize_t>(message.size()) != sent)
        {
            const std::string error = "send() failed: " + std::string(std::strerror(errno));
            ::close(fd);
            throw std::runtime_error(error);
        }

        std::string echoed(message.size(), '\0');
        size_t received = 0;
        while (received < echoed.size())
        {
            const ssize_t n = ::recv(fd, echoed.data() + received, echoed.size() - received, 0);
            if (n <= 0)
            {
                const std::string error = "recv() failed: " + std::string(n < 0 ? std::strerror(errno) : "peer closed early");
                ::close(fd);
                throw std::runtime_error(error);
            }
            received += static_cast<size_t>(n);
        }

        if (echoed != message)
        {
            ::close(fd);
            throw std::runtime_error("echo mismatch");
        }

        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> sleepSeconds(1, 3);
        const int delay = sleepSeconds(rng);
        std::cout << "client " << index << " verified echo, sleep " << delay << "s" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delay));

        ::close(fd);
    }

} // namespace

int main(int argc, char **argv)
{
    int clientCount = 4;
    uint16_t port = 8080;

    if (argc > 1) clientCount = std::stoi(argv[1]);
    if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));
    if (clientCount <= 0)
    {
        std::cerr << "clientCount must be > 0" << std::endl;
        return 1;
    }

    std::mutex readyMutex;
    std::condition_variable readyCond;
    bool serverReady = false;

    std::thread serverThread([&]()
                             {
                                 EventLoop loop;
                                 InetAddress addr(port);
                                 EchoServer server(&loop, addr, "EchoServerDemo2", clientCount);
                                 server.start();

                                 {
                                     std::lock_guard<std::mutex> lock(readyMutex);
                                     serverReady = true;
                                 }
                                 readyCond.notify_one();

                                 loop.loop(); });

    {
        std::unique_lock<std::mutex> lock(readyMutex);
        readyCond.wait(lock, [&]()
                       { return serverReady; });
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(clientCount));

    for (int i = 0; i < clientCount; ++i)
    {
        threads.emplace_back([i, port]()
                             {
                                 try
                                 {
                                     runClient(i + 1, port);
                                 }
                                 catch (const std::exception &ex)
                                 {
                                     std::cerr << "client " << (i + 1) << " failed: " << ex.what() << std::endl;
                                     std::terminate();
                                 } });
    }

    for (auto &thread : threads) thread.join();

    serverThread.join();

    std::cout << "all clients finished" << std::endl;
}

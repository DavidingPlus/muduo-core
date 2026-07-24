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

#include "autoquitechoserver.h"


namespace
{

    // 等待服务端启动后再发起连接。这里做了有限次重试，避免 server 线程刚创建但监听尚未建立时客户端直接失败。
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

            // ECONNREFUSED 表示端口暂时未监听，适合重试；其他错误通常不是时序问题。
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

    // 一个客户端线程完成完整的 echo 验证流程：
    // 1. 连接服务端。
    // 2. 发送唯一消息。
    // 3. 持续读取，直到收齐回显内容。
    // 4. 校验数据一致性后随机 sleep 一会儿，再断开连接。
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

        // TCP 读操作可能分多次返回，所以需要循环收齐整个消息。
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

        // 让不同客户端在不同时间断开，用来覆盖更真实的连接收尾时序。
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
    // 既启动服务端，也在同一进程里启动一组客户端做回环验证。
    int clientCount = 4;
    uint16_t port = 8080;

    if (argc > 1)
    {
        clientCount = std::stoi(argv[1]);
    }
    if (argc > 2)
    {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (clientCount <= 0)
    {
        std::cerr << "clientCount must be > 0" << std::endl;
        return 1;
    }

    // 通过条件变量等待 server 线程至少完成 start()，避免客户端过早发起连接。
    std::mutex readyMutex;
    std::condition_variable readyCond;
    bool serverReady = false;

    std::thread serverThread([&]()
                             {
                                 EventLoop loop;
                                 InetAddress addr(port);

                                 // 该服务端会在“所有预期客户端都连接过且全部断开”后自动退出。
                                 AutoQuitEchoServer server(&loop, addr, "EchoServerDemo2", clientCount);
                                 server.start();

                                 {
                                     std::lock_guard<std::mutex> lock(readyMutex);
                                     serverReady = true;
                                 }
                                 readyCond.notify_one();

                                 loop.loop(); //
                             });

    {
        std::unique_lock<std::mutex> lock(readyMutex);
        readyCond.wait(lock, [&]()
                       { return serverReady; });
    }

    std::vector<std::thread> clientThreads;
    clientThreads.reserve(static_cast<size_t>(clientCount));

    for (int i = 0; i < clientCount; ++i)
    {
        clientThreads.emplace_back([i, port]()
                                   {
                                       try
                                       {
                                           runClient(i + 1, port);
                                       }
                                       catch (const std::exception &ex)
                                       {
                                           std::cerr << "client " << (i + 1) << " failed: " << ex.what() << std::endl;
                                           std::terminate();
                                       } //
                                   });
    }

    // 等所有客户端线程结束后，再等待服务端线程退出。正常情况下，服务端退出由 AutoQuitEchoServer 的连接计数逻辑触发。
    for (auto &clientThread : clientThreads) clientThread.join();

    serverThread.join();

    std::cout << "all clients finished" << std::endl;
}

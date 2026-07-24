#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "acceptor.h"
#include "currentthread.h"
#include "eventloop.h"
#include "inetaddress.h"
#include "nettestutils.h"


using namespace std::chrono_literals;
using namespace NetTestUtils;


// 验证构造后 listen() 会真正创建并暴露一个已绑定的监听 socket。
TEST(AcceptorTest, ConstructorBindsAndListenPublishesSocketOptions)
{
    EventLoop loop;
    Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);

    EXPECT_FALSE(acceptor.listening());

    // listen() 之后再去扫描 /proc/self/fd，确认内核侧状态已经建立。
    acceptor.listen();

    EXPECT_TRUE(acceptor.listening());

    auto info = findListeningSocketInfo();
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->fd >= 0);
    EXPECT_EQ(info->ip, "127.0.0.1");
    EXPECT_NE(info->port, 0);
    EXPECT_TRUE(info->reuseAddr);
    EXPECT_FALSE(info->reusePort);
    EXPECT_TRUE(info->nonblocking);
    EXPECT_TRUE(info->cloexec);
}

// 验证重复调用 listen() 不会重复创建监听 socket。
TEST(AcceptorTest, ListenIsIdempotentAndKeepsSingleListeningSocket)
{
    EventLoop loop;
    Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), true);

    acceptor.listen();
    const int before = countListeningTcpSockets();

    acceptor.listen();
    const int after = countListeningTcpSockets();

    EXPECT_TRUE(acceptor.listening());
    EXPECT_EQ(before, 1);
    EXPECT_EQ(after, 1);
}

// 验证新连接回调能收到对端地址，并且 accept 出来的 fd 已配置为非阻塞和 CLOEXEC。
TEST(AcceptorTest, AcceptCallbackReceivesPeerAddressAndNonblockingFd)
{
    std::promise<uint16_t> portPromise;
    std::promise<AcceptedConnectionInfo> connectionPromise;
    std::promise<pid_t> loopTidPromise;
    std::promise<void> readyPromise;

    // 单独起一个 loop 线程，模拟真实服务端的 accept 流程。
    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               const pid_t loopTid = CurrentThread::tid();
                               Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);

                               // 连接到来后，把 accept 结果和线程信息一起回传给主线程。
                               acceptor.setNewConnectionCallback([&](int sockfd, const InetAddress &peerAddr)
                                                                 {
                                                                     int fdFlags = ::fcntl(sockfd, F_GETFD);
                                                                     int statusFlags = ::fcntl(sockfd, F_GETFL);
                                                                     connectionPromise.set_value(AcceptedConnectionInfo{
                                                                         sockfd,
                                                                         peerAddr.toIp(),
                                                                         peerAddr.toPort(),
                                                                         CurrentThread::tid(),
                                                                         (statusFlags & O_NONBLOCK) != 0,
                                                                         (fdFlags & FD_CLOEXEC) != 0});
                                                                     loop.quit(); });

                               acceptor.listen();
                               auto info = findListeningSocketInfo();
                               ASSERT_TRUE(info.has_value());
                               portPromise.set_value(info->port);
                               loopTidPromise.set_value(loopTid);
                               readyPromise.set_value();
                               loop.loop(); });

    readyPromise.get_future().wait();
    const uint16_t port = portPromise.get_future().get();
    const pid_t loopTid = loopTidPromise.get_future().get();

    ScopedFd clientFd(createTcpSocket());
    connectToPort(clientFd.get(), port);

    auto connection = connectionPromise.get_future();
    ASSERT_EQ(connection.wait_for(1s), std::future_status::ready);
    const AcceptedConnectionInfo info = connection.get();

    EXPECT_EQ(info.ip, "127.0.0.1");
    EXPECT_NE(info.port, 0);
    EXPECT_EQ(info.tid, loopTid);
    EXPECT_TRUE(info.nonblocking);
    EXPECT_TRUE(info.cloexec);

    ::close(info.fd);
    loopThread.join();
}

// 验证 Acceptor 能连续接收多个连接，并分别回调每个新 fd。
TEST(AcceptorTest, AcceptCallbackHandlesMultipleConnections)
{
    std::promise<uint16_t> portPromise;
    std::promise<void> readyPromise;
    std::promise<void> finishedPromise;
    std::mutex mutex;
    std::vector<AcceptedConnectionInfo> accepted;

    // worker 线程持续 accept，直到收到两个连接再退出 loop。
    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);

                               // 连续 accept 两次，确认回调会被多次触发。
                               acceptor.setNewConnectionCallback([&](int sockfd, const InetAddress &peerAddr)
                                                                 {
                                                                     int fdFlags = ::fcntl(sockfd, F_GETFD);
                                                                     int statusFlags = ::fcntl(sockfd, F_GETFL);
                                                                     {
                                                                         std::lock_guard<std::mutex> lock(mutex);
                                                                         accepted.push_back(AcceptedConnectionInfo{
                                                                             sockfd,
                                                                             peerAddr.toIp(),
                                                                             peerAddr.toPort(),
                                                                             CurrentThread::tid(),
                                                                             (statusFlags & O_NONBLOCK) != 0,
                                                                             (fdFlags & FD_CLOEXEC) != 0});
                                                                     }

                                                                     if (accepted.size() >= 2) loop.quit(); });

                               acceptor.listen();
                               auto info = findListeningSocketInfo();
                               ASSERT_TRUE(info.has_value());
                               portPromise.set_value(info->port);
                               readyPromise.set_value();
                               loop.loop();
                               finishedPromise.set_value(); });

    readyPromise.get_future().wait();
    const uint16_t port = portPromise.get_future().get();

    ScopedFd client1(createTcpSocket());
    ScopedFd client2(createTcpSocket());
    connectToPort(client1.get(), port);
    connectToPort(client2.get(), port);

    ASSERT_EQ(finishedPromise.get_future().wait_for(1s), std::future_status::ready);
    loopThread.join();

    ASSERT_EQ(accepted.size(), 2u);
    EXPECT_EQ(accepted[0].ip, "127.0.0.1");
    EXPECT_EQ(accepted[1].ip, "127.0.0.1");
    EXPECT_NE(accepted[0].port, accepted[1].port);
    EXPECT_TRUE(accepted[0].nonblocking);
    EXPECT_TRUE(accepted[1].nonblocking);
    EXPECT_TRUE(accepted[0].cloexec);
    EXPECT_TRUE(accepted[1].cloexec);

    ::close(accepted[0].fd);
    ::close(accepted[1].fd);
}

// 验证没有设置回调时，Acceptor 会自动关闭已 accept 的连接。
TEST(AcceptorTest, NoCallbackClosesAcceptedConnection)
{
    std::promise<uint16_t> portPromise;
    std::promise<void> readyPromise;
    std::promise<EventLoop *> loopPromise;

    // 不设置回调，直接验证 Acceptor 的兜底关闭逻辑。
    std::thread loopThread([&]()
                           {
                               EventLoop loop;
                               Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"), false);
                               acceptor.listen();

                               auto info = findListeningSocketInfo();
                               ASSERT_TRUE(info.has_value());
                               portPromise.set_value(info->port);
                               readyPromise.set_value();
                               loopPromise.set_value(&loop);

                               loop.loop(); });

    readyPromise.get_future().wait();
    const uint16_t port = portPromise.get_future().get();

    ScopedFd clientFd(createTcpSocket());
    connectToPort(clientFd.get(), port);

    ASSERT_GT(pollReadableOrHangup(clientFd.get(), 1000), 0);

    char byte = '\0';
    EXPECT_EQ(::recv(clientFd.get(), &byte, sizeof(byte), 0), 0);

    EventLoop *loop = loopPromise.get_future().get();
    loop->quit();
    loopThread.join();
}

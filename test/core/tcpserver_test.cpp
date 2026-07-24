#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "buffer.h"
#include "currentthread.h"
#include "eventloop.h"
#include "eventloopthread.h"
#include "tcpconnection.h"
#include "tcpserver.h"

#include "nettestutils.h"


using namespace std::chrono_literals;
using namespace NetTestUtils;


// 验证单线程 TcpServer 能完整跑通连接建立、消息处理、回写完成和连接断开回调。
TEST(TcpServerTest, SingleThreadServerPropagatesCoreCallbacks)
{
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::promise<void> connectedPromise;
    std::promise<std::string> messagePromise;
    std::promise<void> writeCompletePromise;
    std::promise<void> disconnectedPromise;
    std::atomic_bool connectedSet = false;
    std::atomic_bool messageSet = false;
    std::atomic_bool writeCompleteSet = false;
    std::atomic_bool disconnectedSet = false;
    auto connectedFuture = connectedPromise.get_future();
    auto messageFuture = messagePromise.get_future();
    auto writeCompleteFuture = writeCompletePromise.get_future();
    auto disconnectedFuture = disconnectedPromise.get_future();

    uint16_t port = 0;
    auto server = createAndStartServer(
        loop, 0, [&](TcpServer &tcpServer)
        {
                                           tcpServer.setConnectionCallback([&](const TcpConnectionPtr &conn)
                                                                           {
                                                                               if (conn->connected())
                                                                               {
                                                                                   if (!connectedSet.exchange(true)) connectedPromise.set_value();
                                                                               }
                                                                               else
                                                                               {
                                                                                   if (!disconnectedSet.exchange(true)) disconnectedPromise.set_value();
                                                                               } });
                                           tcpServer.setMessageCallback([&](const TcpConnectionPtr &conn, Buffer &buffer, const Timestamp &)
                                                                        {
                                                                            const std::string payload(buffer.peek(), buffer.readableBytes());
                                                                            buffer.retrieveAll();
                                                                            conn->send(payload);
                                                                            if (!messageSet.exchange(true)) messagePromise.set_value(payload); });
                                           tcpServer.setWriteCompleteCallback([&](const TcpConnectionPtr &)
                                                                              {
                                                                                  if (!writeCompleteSet.exchange(true)) writeCompletePromise.set_value(); }); },
        &port);

    ScopedFd client(connectClientToPort(port));

    EXPECT_EQ(connectedFuture.wait_for(2s), std::future_status::ready);

    const std::string payload = "echo-me";
    ASSERT_EQ(::write(client.get(), payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    EXPECT_EQ(messageFuture.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(messageFuture.get(), payload);
    EXPECT_EQ(readExactly(client.get(), payload.size()), payload);
    EXPECT_EQ(writeCompleteFuture.wait_for(2s), std::future_status::ready);

    client.reset();
    EXPECT_EQ(disconnectedFuture.wait_for(2s), std::future_status::ready);

    destroyServerOnLoop(loop, server);
}

// 验证 start() 幂等：单线程模式下多次调用不会重复启动，threadInitCallback 也只执行一次。
TEST(TcpServerTest, StartIsIdempotentInSingleThreadMode)
{
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::mutex mutex;
    std::vector<pid_t> initTids;
    std::promise<void> initPromise;
    std::atomic_bool initSet = false;
    std::promise<pid_t> mainLoopTidPromise;
    auto initFuture = initPromise.get_future();

    loop->runInLoop([&]()
                    { mainLoopTidPromise.set_value(CurrentThread::tid()); });
    const pid_t mainLoopTid = mainLoopTidPromise.get_future().get();

    uint16_t port = 0;
    auto server = createAndStartServer(
        loop, 0, [&](TcpServer &tcpServer)
        {
                                           tcpServer.setConnectionCallback([](const TcpConnectionPtr &) {});
                                           tcpServer.setThreadInitCallback([&](EventLoop *)
                                                                           {
                                                                               {
                                                                                   std::lock_guard<std::mutex> lock(mutex);
                                                                                   initTids.push_back(CurrentThread::tid());
                                                                               }
                                                                               if (!initSet.exchange(true)) initPromise.set_value(); });
                                           tcpServer.start(); }, // 第一轮 start 之后，再通过 createAndStartServer 内部再调用一次 start()。
        &port);

    EXPECT_EQ(initFuture.wait_for(2s), std::future_status::ready);

    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(initTids.size(), 1u);
        EXPECT_EQ(initTids[0], mainLoopTid);
    }

    ScopedFd client(connectClientToPort(port));
    destroyServerOnLoop(loop, server);
}

// 验证多线程 TcpServer 会按 Round-Robin 将新连接分发到不同 worker loop。
TEST(TcpServerTest, MultiThreadServerDistributesConnectionsAcrossWorkerLoops)
{
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<EventLoop *> assignedLoops;
    std::vector<pid_t> initTids;
    std::promise<void> initDonePromise;
    std::atomic<int> initCount = 0;
    std::atomic_bool initDoneSet = false;
    std::atomic<int> disconnectedCount = 0;
    std::promise<void> disconnectedAllPromise;
    std::atomic_bool disconnectedAllSet = false;
    auto initDoneFuture = initDonePromise.get_future();
    auto disconnectedAllFuture = disconnectedAllPromise.get_future();

    uint16_t port = 0;
    auto server = createAndStartServer(
        loop, 2, [&](TcpServer &tcpServer)
        {
                                           tcpServer.setThreadInitCallback([&](EventLoop *)
                                                                           {
                                                                               {
                                                                                   std::lock_guard<std::mutex> lock(mutex);
                                                                                   initTids.push_back(CurrentThread::tid());
                                                                               }
                                                                               if (2 == initCount.fetch_add(1) + 1 && !initDoneSet.exchange(true)) initDonePromise.set_value(); });
                                           tcpServer.setConnectionCallback([&](const TcpConnectionPtr &conn)
                                                                           {
                                                                               if (conn->connected())
                                                                               {
                                                                                   std::lock_guard<std::mutex> lock(mutex);
                                                                                   assignedLoops.push_back(conn->getLoop());
                                                                                   cv.notify_all();
                                                                               }
                                                                               else if (4 == disconnectedCount.fetch_add(1) + 1 && !disconnectedAllSet.exchange(true))
                                                                               {
                                                                                   disconnectedAllPromise.set_value();
                                                                               } }); },
        &port);

    EXPECT_EQ(initDoneFuture.wait_for(2s), std::future_status::ready);
    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(initTids.size(), 2u);
        EXPECT_NE(initTids[0], initTids[1]);
    }

    std::vector<ScopedFd> clients;
    clients.reserve(4);
    for (int i = 0; i < 4; ++i)
    {
        clients.emplace_back(connectClientToPort(port));
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, 2s, [&]()
                                { return assignedLoops.size() == static_cast<size_t>(i + 1); }));
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(assignedLoops.size(), 4u);
        EXPECT_NE(assignedLoops[0], assignedLoops[1]);
        EXPECT_EQ(assignedLoops[0], assignedLoops[2]);
        EXPECT_EQ(assignedLoops[1], assignedLoops[3]);
    }

    for (auto &client : clients) client.reset();

    EXPECT_EQ(disconnectedAllFuture.wait_for(2s), std::future_status::ready);

    destroyServerOnLoop(loop, server);
}

// 验证在仍有活动连接时销毁 TcpServer，会主动关闭连接并通知客户端看到 EOF。
TEST(TcpServerTest, DestroyingServerClosesActiveConnections)
{
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::promise<void> connectedPromise;
    std::promise<void> disconnectedPromise;
    std::atomic_bool connectedSet = false;
    std::atomic_bool disconnectedSet = false;
    auto connectedFuture = connectedPromise.get_future();
    auto disconnectedFuture = disconnectedPromise.get_future();

    uint16_t port = 0;
    auto server = createAndStartServer(
        loop, 0, [&](TcpServer &tcpServer)
        { tcpServer.setConnectionCallback([&](const TcpConnectionPtr &conn)
                                          {
                                                                               if (conn->connected())
                                                                               {
                                                                                   if (!connectedSet.exchange(true)) connectedPromise.set_value();
                                                                               }
                                                                               else
                                                                               {
                                                                                   if (!disconnectedSet.exchange(true)) disconnectedPromise.set_value();
                                                                               } }); },
        &port);

    ScopedFd client(connectClientToPort(port));
    EXPECT_EQ(connectedFuture.wait_for(2s), std::future_status::ready);

    destroyServerOnLoop(loop, server);

    const short revents = waitForFdEvent(client.get(), POLLIN | POLLHUP, 2000);
    EXPECT_TRUE((revents & (POLLIN | POLLHUP)) != 0);

    char byte = '\0';
    EXPECT_EQ(::read(client.get(), &byte, 1), 0);
    EXPECT_EQ(disconnectedFuture.wait_for(2s), std::future_status::ready);
}

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "currentthread.h"
#include "eventloop.h"
#include "eventloopthread.h"
#include "netutils.h"
#include "tcpconnection.h"
#include "timestamp.h"

#include "nettestutils.h"


using namespace std::chrono_literals;
using namespace NetTestUtils;


// 验证 connectEstablished()/connectDestroyed() 会正确切换状态并只各自触发一次连接状态回调。
TEST(TcpConnectionTest, LifecycleCallbacksReflectConnectedState)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoop loop;

    std::vector<bool> states;

    auto conn = std::make_shared<TcpConnection>(&loop, "lifecycle-conn", sockets.server.release(), sockets.localAddr, sockets.peerAddr);
    conn->setConnectionCallback([&](const TcpConnectionPtr &callbackConn)
                                { states.push_back(callbackConn->connected()); });

    conn->connectEstablished();
    conn->connectDestroyed();

    ASSERT_EQ(states.size(), 2u);
    EXPECT_TRUE(states[0]);
    EXPECT_FALSE(states[1]);
}

// 验证对端发来的数据会触发 messageCallback，并且业务层可以消费 Buffer 中的可读数据。
TEST(TcpConnectionTest, MessageCallbackReceivesReadableBuffer)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::promise<std::string> payloadPromise;
    std::atomic_bool payloadSet = false;
    auto payloadFuture = payloadPromise.get_future();

    auto conn = createEstablishedConnection(loop, sockets, [&](const TcpConnectionPtr &connection)
                                            {
                                                connection->setConnectionCallback([](const TcpConnectionPtr &) {});
                                                connection->setMessageCallback([&](const TcpConnectionPtr &, Buffer &buffer, const Timestamp &receiveTime)
                                                                               {
                                                                                   EXPECT_GT(receiveTime.microSecondsSinceEpoch(), 0);
                                                                                   const std::string payload(buffer.peek(), buffer.readableBytes());
                                                                                   buffer.retrieveAll();
                                                                                   if (!payloadSet.exchange(true)) payloadPromise.set_value(payload); }); });

    const std::string message = "hello from peer";
    ASSERT_EQ(::write(sockets.client.get(), message.data(), message.size()), static_cast<ssize_t>(message.size()));

    EXPECT_EQ(payloadFuture.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(payloadFuture.get(), message);

    destroyConnection(loop, conn);
}

// 验证 send() 会把数据送到对端，并在发送完成后触发 writeCompleteCallback。
TEST(TcpConnectionTest, SendDeliversDataAndTriggersWriteComplete)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::promise<void> writeCompletePromise;
    std::atomic_bool writeCompleteSet = false;
    auto writeCompleteFuture = writeCompletePromise.get_future();

    auto conn = createEstablishedConnection(loop, sockets, [&](const TcpConnectionPtr &connection)
                                            {
                                                connection->setConnectionCallback([](const TcpConnectionPtr &) {});
                                                connection->setWriteCompleteCallback([&](const TcpConnectionPtr &)
                                                                                     {
                                                                                         if (!writeCompleteSet.exchange(true)) writeCompletePromise.set_value(); }); });

    const std::string payload = "server-payload";
    conn->send(payload);

    EXPECT_EQ(readExactly(sockets.client.get(), payload.size()), payload);
    EXPECT_EQ(writeCompleteFuture.wait_for(2s), std::future_status::ready);

    destroyConnection(loop, conn);
}

// 验证 shutdown() 会在已排队数据发完后半关闭连接，并阻止后续新的业务发送请求。
TEST(TcpConnectionTest, ShutdownFlushesPendingDataAndPreventsFurtherSends)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    auto conn = createEstablishedConnection(loop, sockets, [&](const TcpConnectionPtr &connection)
                                            { connection->setConnectionCallback([](const TcpConnectionPtr &) {}); });

    conn->send("first");
    conn->shutdown();
    conn->send("second");

    EXPECT_EQ(readUntilEof(sockets.client.get()), "first");

    sockets.client.reset();
    destroyConnection(loop, conn);
}

// 验证对端主动关闭连接时，disconnect 回调和 close 回调的时序正确，且 connectDestroyed() 不会重复触发 disconnect。
TEST(TcpConnectionTest, RemoteCloseInvokesDisconnectAndCloseCallbacksOnce)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> events;
    std::promise<void> closedPromise;
    std::atomic_bool closedSet = false;
    auto closedFuture = closedPromise.get_future();

    auto conn = createEstablishedConnection(loop, sockets, [&](const TcpConnectionPtr &connection)
                                            {
                                                connection->setConnectionCallback([&](const TcpConnectionPtr &callbackConn)
                                                                                  {
                                                                                      std::lock_guard<std::mutex> lock(mutex);
                                                                                      events.emplace_back(callbackConn->connected() ? "connected" : "disconnected");
                                                                                      cv.notify_all(); });
                                                connection->setCloseCallback([&](const TcpConnectionPtr &callbackConn)
                                                                             {
                                                                                 {
                                                                                     std::lock_guard<std::mutex> lock(mutex);
                                                                                     events.emplace_back("close");
                                                                                     cv.notify_all();
                                                                                 }
                                                                                 callbackConn->connectDestroyed();
                                                                                 if (!closedSet.exchange(true)) closedPromise.set_value(); }); });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, 2s, [&]()
                                { return !events.empty(); }));
    }

    sockets.client.reset();

    EXPECT_EQ(closedFuture.wait_for(2s), std::future_status::ready);

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0], "connected");
    EXPECT_EQ(events[1], "disconnected");
    EXPECT_EQ(events[2], "close");
}

// 验证 sendFile() 能把文件内容完整发送给对端，并触发 writeCompleteCallback。
TEST(TcpConnectionTest, SendFileTransfersEntireFile)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    std::promise<void> writeCompletePromise;
    std::atomic_bool writeCompleteSet = false;
    auto writeCompleteFuture = writeCompletePromise.get_future();

    auto conn = createEstablishedConnection(loop, sockets, [&](const TcpConnectionPtr &connection)
                                            {
                                                connection->setConnectionCallback([](const TcpConnectionPtr &) {});
                                                connection->setWriteCompleteCallback([&](const TcpConnectionPtr &)
                                                                                     {
                                                                                         if (!writeCompleteSet.exchange(true)) writeCompletePromise.set_value(); }); });

    const std::string fileContent =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Vestibulum feugiat, metus at efficitur pretium, est augue vehicula nisl.";
    ScopedFd file = createTempFileWithContent(fileContent);

    conn->sendFile(file.get(), 0, fileContent.size());

    EXPECT_EQ(readExactly(sockets.client.get(), fileContent.size()), fileContent);
    EXPECT_EQ(writeCompleteFuture.wait_for(2s), std::future_status::ready);

    destroyConnection(loop, conn);
}

// 验证当发送积压跨过高水位时会触发 highWaterMarkCallback，随后数据仍能继续发完。
TEST(TcpConnectionTest, HighWaterMarkCallbackFiresWhenOutputBacklogCrossesThreshold)
{
    ConnectedTcpSockets sockets = createConnectedTcpSockets();
    EventLoopThread loopThread;
    EventLoop *loop = loopThread.startLoop();

    const int smallBuffer = 1024;
    ASSERT_EQ(::setsockopt(sockets.server.get(), SOL_SOCKET, SO_SNDBUF, &smallBuffer, sizeof(smallBuffer)), 0);
    ASSERT_EQ(::setsockopt(sockets.client.get(), SOL_SOCKET, SO_RCVBUF, &smallBuffer, sizeof(smallBuffer)), 0);

    std::promise<size_t> highWaterPromise;
    std::atomic_bool highWaterSet = false;

    auto conn = createEstablishedConnection(loop, sockets, [&](const TcpConnectionPtr &connection)
                                            {
                                                connection->setConnectionCallback([](const TcpConnectionPtr &) {});
                                                connection->setHighWaterMarkCallback([&](const TcpConnectionPtr &, size_t queuedBytes)
                                                                                     {
                                                                                         if (!highWaterSet.exchange(true)) highWaterPromise.set_value(queuedBytes); },
                                                                                     4096); });

    const std::string payload(256 * 1024, 'x');
    conn->send(payload);

    auto highWaterFuture = highWaterPromise.get_future();
    EXPECT_EQ(highWaterFuture.wait_for(2s), std::future_status::ready);
    EXPECT_GE(highWaterFuture.get(), 4096u);

    const std::string receivedPrefix = readExactly(sockets.client.get(), 8192);
    ASSERT_EQ(receivedPrefix.size(), 8192u);
    EXPECT_EQ(receivedPrefix, std::string(receivedPrefix.size(), 'x'));

    destroyConnection(loop, conn);
}

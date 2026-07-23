#include <gtest/gtest.h>

#include <cerrno>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "buffer.h"


namespace
{

    // 用 pipe 提供一个最简单的读写 fd，避免依赖网络栈。
    class Pipe
    {

    public:

        Pipe()
        {
            if (::pipe(m_fds) != 0) throw std::runtime_error("pipe failed");
        }

        ~Pipe()
        {
            if (m_fds[0] >= 0) ::close(m_fds[0]);
            if (m_fds[1] >= 0) ::close(m_fds[1]);
        }

        int readEnd() const { return m_fds[0]; }

        int writeEnd() const { return m_fds[1]; }


    private:

        int m_fds[2] = {-1, -1};
    };

} // namespace


// 验证 Buffer 初始时没有可读数据，读写指针位于同一位置。
TEST(BufferTest, DefaultState)
{
    Buffer buffer;

    EXPECT_EQ(buffer.readableBytes(), 0);
    EXPECT_EQ(buffer.writableBytes(), Buffer::kInitialSize);
    EXPECT_EQ(buffer.peek(), buffer.beginWrite());
}

// 验证 append 后可读数据增加，retrieve 部分字节后读指针前移。
TEST(BufferTest, AppendAndRetrievePartialData)
{
    Buffer buffer;
    const std::string data = "hello";

    buffer.append(data.data(), data.size());

    ASSERT_EQ(buffer.readableBytes(), data.size());
    EXPECT_EQ(buffer.writableBytes(), Buffer::kInitialSize - data.size());
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), data);

    buffer.retrieve(2);

    ASSERT_EQ(buffer.readableBytes(), 3);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "llo");
}

// 验证 retrieveAll() 会把 Buffer 恢复到初始空状态。
TEST(BufferTest, RetrieveAllResetsIndices)
{
    Buffer buffer;
    const std::string data = "abcdef";

    buffer.append(data.data(), data.size());
    buffer.retrieve(3);
    buffer.retrieveAll();

    EXPECT_EQ(buffer.readableBytes(), 0);
    EXPECT_EQ(buffer.writableBytes(), Buffer::kInitialSize);
    EXPECT_EQ(buffer.peek(), buffer.beginWrite());
}

// 验证 retrieveAsString() 会消费指定字节，并保留剩余内容。
TEST(BufferTest, RetrieveAsStringConsumesRequestedBytes)
{
    Buffer buffer;
    const std::string data = "abcdef";

    buffer.append(data.data(), data.size());

    EXPECT_EQ(buffer.retrieveAsString(2), "ab");
    ASSERT_EQ(buffer.readableBytes(), 4);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "cdef");

    EXPECT_EQ(buffer.retrieveAllAsString(), "cdef");
    EXPECT_EQ(buffer.readableBytes(), 0);
    EXPECT_EQ(buffer.writableBytes(), Buffer::kInitialSize);
}

// 验证可写空间不足时，Buffer 会优先复用已消费空间而不是立刻扩容。
TEST(BufferTest, EnsureWritableBytesReusesConsumedSpaceWithoutGrowing)
{
    Buffer buffer(16);
    const std::string first = "1234567890";
    const std::string second = "ABCDEFGH";

    buffer.append(first.data(), first.size());
    buffer.retrieve(8);

    ASSERT_EQ(buffer.readableBytes(), 2);
    ASSERT_EQ(buffer.writableBytes(), 6);

    buffer.ensureWritableBytes(second.size());
    buffer.append(second.data(), second.size());

    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "90ABCDEFGH");
    EXPECT_EQ(buffer.readableBytes() + buffer.writableBytes(), 16);
    EXPECT_EQ(buffer.writableBytes(), 6);
}

// 验证当总空闲空间不够时，Buffer 会真正扩容。
TEST(BufferTest, EnsureWritableBytesGrowsWhenTotalFreeSpaceIsInsufficient)
{
    Buffer buffer(8);
    const std::string initial = "12345678";
    const std::string extra = "abcdef";

    buffer.append(initial.data(), initial.size());
    buffer.retrieve(2);

    ASSERT_EQ(buffer.readableBytes(), 6);
    ASSERT_EQ(buffer.writableBytes(), 0);

    buffer.ensureWritableBytes(extra.size());
    buffer.append(extra.data(), extra.size());

    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "345678abcdef");
    EXPECT_EQ(std::string(buffer.peek(), 6), "345678");
    EXPECT_GT(buffer.readableBytes() + buffer.writableBytes(), 8);
    EXPECT_EQ(buffer.writableBytes(), 0);
}

// 验证 readFd() 在写空间足够时直接把 fd 数据读入 Buffer。
TEST(BufferTest, ReadFdAppendsWhenWritableSpaceIsEnough)
{
    Pipe pipe;
    Buffer buffer(32);
    const std::string payload = "network-data";

    ASSERT_EQ(::write(pipe.writeEnd(), payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    int savedErrno = 0;
    const ssize_t n = buffer.readFd(pipe.readEnd(), savedErrno);

    ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(savedErrno, 0);
    EXPECT_EQ(buffer.readableBytes(), payload.size());
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), payload);
}

// 验证 readFd() 在写空间不足时会借助额外缓冲区读取完整数据。
TEST(BufferTest, ReadFdUsesExtraBufferWhenPayloadExceedsWritableSpace)
{
    Pipe pipe;
    Buffer buffer(8);
    const std::string payload = "abcdefghijklmnopqrst";

    ASSERT_EQ(::write(pipe.writeEnd(), payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    int savedErrno = 0;
    const ssize_t n = buffer.readFd(pipe.readEnd(), savedErrno);

    ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(savedErrno, 0);
    EXPECT_EQ(buffer.readableBytes(), payload.size());
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), payload);
}

// 验证 readFd() 出错时会把 errno 透传到调用者。
TEST(BufferTest, ReadFdStoresErrnoOnFailure)
{
    Buffer buffer;
    int savedErrno = 0;

    EXPECT_EQ(buffer.readFd(-1, savedErrno), -1);
    EXPECT_EQ(savedErrno, EBADF);
}

// 验证 writeFd() 只发送当前可读数据，但不会消费它们。
TEST(BufferTest, WriteFdWritesReadableBytesWithoutConsumingThem)
{
    Pipe pipe;
    Buffer buffer;
    const std::string payload = "write-through";

    buffer.append(payload.data(), payload.size());

    int savedErrno = 0;
    const ssize_t n = buffer.writeFd(pipe.writeEnd(), savedErrno);

    ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(savedErrno, 0);
    EXPECT_EQ(buffer.readableBytes(), payload.size());

    std::string received(payload.size(), '\0');
    ASSERT_EQ(::read(pipe.readEnd(), received.data(), received.size()), static_cast<ssize_t>(received.size()));
    EXPECT_EQ(received, payload);
}

// 验证 writeFd() 出错时同样会返回 errno。
TEST(BufferTest, WriteFdStoresErrnoOnFailure)
{
    Buffer buffer;
    const std::string payload = "data";

    buffer.append(payload.data(), payload.size());

    int savedErrno = 0;
    EXPECT_EQ(buffer.writeFd(-1, savedErrno), -1);
    EXPECT_EQ(savedErrno, EBADF);
}

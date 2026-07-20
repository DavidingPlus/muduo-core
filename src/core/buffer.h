#ifndef _MUDUO_CORE_m_bufferH_
#define _MUDUO_CORE_m_bufferH_

#include <vector>
#include <string>


/**
 * @class Buffer
 * @brief Buffer 类其实是封装了一个用户缓冲区，以及向这个缓冲区写数据读数据等一系列控制方法。
 *
 * Buffer 使用一块连续内存，通过 m_readerIndex 和 m_writerIndex 两个索引分别表示读位置和写位置。
 *
 * 内存布局：
 *
 * | prependable | readable | writable |
 *              ^          ^
 *              |          |
 *        readerIndex  writerIndex
 *
 * readable 区域表示已经接收的数据；writable 区域表示可以写入的数据。
 *
 * 读取数据时不会移动内存，只移动 m_readerIndex，避免频繁 memcpy，提高网络 IO 性能。
 *
 * 当可写空间不足时：
 * 1. 优先利用前面已经读取的数据空间；
 * 2. 如果空间仍然不足，则扩容。
 *
 * 前面预留 prependable 空间，用于存放协议头，例如长度字段，避免插入数据时移动已有内容。
 */
class Buffer
{

public:

    // 初始预留的 prependable 空间大小。
    static constexpr size_t kPrependSize = 8;

    // 初始预留的 readable 和 writable 数据空间大小。初始时 readableBytes 为 0，writableBytes 为 kInitialSize，这个是符合语义的。
    static constexpr size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize) : m_buffer(kPrependSize + initialSize), m_readerIndex(kPrependSize), m_writerIndex(kPrependSize) {}

    ~Buffer() = default;

    size_t readableBytes() const { return m_writerIndex - m_readerIndex; }

    size_t writableBytes() const { return m_buffer.size() - m_writerIndex; }

    size_t prependableBytes() const { return m_readerIndex; }

    // 返回缓冲区中可读数据的起始地址。
    const char *peek() const { return begin() + m_readerIndex; }

    void retrieve(size_t len);

    void retrieveAll();

    // 把 onMessage 函数上报的 Buffer 数据，转成 string 类型的数据返回。
    std::string retrieveAllAsString();

    std::string retrieveAsString(size_t len);

    // m_buffer.size - m_writerIndex
    void ensureWritableBytes(size_t len);

    // 把 [data, data + len] 内存上的数据添加到 writable 缓冲区当中。
    void append(const char *data, size_t len);

    char *beginWrite() { return begin() + m_writerIndex; }

    const char *beginWrite() const { return begin() + m_writerIndex; }

    // 从 fd 上读取数据。
    ssize_t readFd(int fd, int *saveErrno);

    // 通过 fd 发送数据。
    ssize_t writeFd(int fd, int *saveErrno);


private:

    // vector 底层数组首元素的地址，也就是数组的起始地址。
    char *begin() { return &*m_buffer.begin(); }

    const char *begin() const { return &*m_buffer.begin(); }

    void makeSpace(size_t len);


    std::vector<char> m_buffer;

    size_t m_readerIndex;

    size_t m_writerIndex;
};


#endif

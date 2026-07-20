#include "buffer.h"


void Buffer::retrieve(size_t len)
{
    // 说明应用只读取了可读缓冲区数据的一部分，就是 len 长度，还剩下 [m_readerIndex+=len, m_writerIndex] 的数据未读。
    if (len < readableBytes())
    {
        m_readerIndex += len;
    }
    // len == readableBytes()
    else
    {
        retrieveAll();
    }
}

void Buffer::retrieveAll()
{
    // 消费完全部可读数据以后，Buffer 的状态应该回到初始状态。这也是为什么不能直接调用 retrieve(readableBytes())。
    m_readerIndex = kPrependSize;
    m_writerIndex = kPrependSize;
}

std::string Buffer::retrieveAsString(size_t len)
{
    std::string res(peek(), len);
    retrieve(len);
    return res;
}

#include "eventloopthread.h"

#include "eventloop.h"


EventLoopThread::~EventLoopThread()
{
    m_exiting = true;

    if (m_loop)
    {
        m_loop->quit();
        m_thread.join();
    }
}

EventLoop *EventLoopThread::startLoop()
{
    // 启用底层线程 Thread 类对象 m_thread 中通过 start() 创建的线程，执行的函数就是 threadFunc()。
    m_thread.start();

    EventLoop *loop = nullptr;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this]()
                    { return m_loop != nullptr; });
        loop = m_loop;
    }


    return loop;
}

void EventLoopThread::threadFunc()
{
    // 创建一个独立的 EventLoop 对象，和上面的线程是一一对应的 one loop per thread。
    EventLoop loop;

    if (m_callback) m_callback(&loop);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_loop = &loop;
        m_cond.notify_one();
    }

    // 执行 EventLoop 的 loop() 开启了底层的 Poller 的 poll()。
    loop.loop();

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_loop = nullptr;
    }
}

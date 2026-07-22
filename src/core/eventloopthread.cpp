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
    // 该函数首先创建工作线程，随后等待工作线程完成 EventLoop 的创建。由于 EventLoop 必须在线程内部构造，因此 m_thread.start() 返回时，EventLoop 可能尚未初始化完成，不能立即返回 loop 指针。

    // 启用底层线程 Thread 类对象 m_thread 中通过 start() 创建的线程，执行的函数就是 threadFunc()。
    m_thread.start();

    EventLoop *loop = nullptr;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // 这里通过条件变量等待 threadFunc() 完成 EventLoop 的创建，并将 m_loop 设置为有效地址后再返回。
        m_cond.wait(lock, [this]()
                    { return m_loop != nullptr; });
        loop = m_loop;
    }


    return loop;
}

void EventLoopThread::threadFunc()
{
    // 创建一个独立的 EventLoop 对象，和上面的线程是一一对应的 one loop per thread。
    // 注意，EventLoop 是线程内的局部对象，其生命周期与工作线程一致，EventLoopThread 仅保存其地址，不拥有该对象。
    EventLoop loop;

    // EventLoop 创建完成后，会先执行用户提供的初始化回调（如果有），然后将 EventLoop 的地址保存到 m_loop，并通知正在 startLoop() 中等待的线程。最后调用 EventLoop::loop() 进入事件循环。当 EventLoop 收到 quit() 请求后，loop() 返回，EventLoop 随之析构，工作线程也结束运行。当 EventLoop 收到 quit() 请求后，loop() 返回，EventLoop 随之析构，工作线程也结束运行。

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

#include "eventloopthreadpool.h"

#include "eventloop.h"
#include "eventloopthread.h"

#include <cassert>


/*
                     Main Reactor
                         |
                         |
                     listen socket
                         |
                         |
                  accept connection
                         |
                         |
                 EventLoopThreadPool
                         |
        +----------------+----------------+
        |                |                |
        |                |                |
        v                v                v
     Sub Reactor 0    Sub Reactor 1    Sub Reactor 2

     EventLoop        EventLoop        EventLoop
        |                |                |
        |                |                |
     Channel          Channel          Channel
        |                |                |
        |                |                |
     TCP Conn         TCP Conn         TCP Conn
*/

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    // 一个线程池不能重复 start，并且 start 必须在 mainLoop 所在线程（一般是主线程）执行。
    assert(!m_started && m_mainLoop->isInLoopThread());

    m_started = true;

    for (int i = 0; i < m_numThreads; ++i)
    {
        // 一个 EventLoopThread 对应一个 IO 工作线程以及该线程内部创建的一个 EventLoop。注意：
        // 1. EventLoop 对象不是在当前线程创建的，而是在 EventLoopThread 启动的新线程中创建。IO thread -> EventLoop loop;（栈对象）。
        // 2. startLoop() 会启动线程，并返回该线程中 EventLoop 对象的地址。
        // 3. m_subLoops 只保存 EventLoop 地址，不负责对象生命周期管理。因为 EventLoop 是线程栈对象，它的生命周期依赖于对应线程持续运行。
        // 4. m_threads 保存 EventLoopThread 对象，负责维持 IO 线程生命周期。只要 EventLoopThread 存在，对应线程就不会退出，线程中的 EventLoop 也一直有效，因此 m_subLoops 中保存的指针才安全。
        // 关系如下：m_threads 存储 EventLoopThread 的集合，EventLoopThread 创建并运行 EventLoop，EventLoop 执行 startLoop() 后返回栈对象指针存储于集合 m_subLoops 中。

        EventLoopThread *t = new EventLoopThread(cb, m_name + std::to_string(i));
        m_threads.emplace_back(std::unique_ptr<EventLoopThread>(t));
        m_subLoops.emplace_back(t->startLoop());
    }

    // 单线程模式下 mainLoop 作为唯一 Reactor。mainLoop 属于创建 TcpServer 的线程（通常是主线程），不能通过 EventLoopThread::startLoop() 创建，因为 startLoop() 的语义是创建新的 IO 工作线程，并在线程内部创建对应的 EventLoop。mainLoop 已经由用户线程创建，后续由用户主动调用 loop() 启动事件循环。因此这里需要手动执行线程初始化回调。
    // 多线程模式下，EventLoopThread 在线程内部创建 subLoop 后，会自动执行传入的初始化回调。
    if (0 == m_numThreads && cb) cb(m_mainLoop);
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    // 如果只设置一个线程，也就是只有一个 mainReactor，无 subReactor。那么轮询只有一个线程 getNextLoop() 每次都返回当前的 m_mainLoop。
    EventLoop *loop = m_mainLoop;

    // 获取负责处理下一个新连接的 EventLoop。在多线程模式下，以 Round-Robin（轮询）的方式选择一个 subLoop；单线程模式下始终返回 mainLoop。
    if (!m_subLoops.empty())
    {
        loop = m_subLoops[m_next++];
        // 轮询。
        if (m_next >= m_subLoops.size()) m_next = 0;
    }


    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (m_subLoops.empty())
    {
        return {m_mainLoop};
    }
    else
    {
        return m_subLoops;
    }
}

#ifndef _MUDUO_CORE_EVENTLOOPTHREADPOOL_H_
#define _MUDUO_CORE_EVENTLOOPTHREADPOOL_H_

#include "globalmacros.h"

#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;


// EventLoopThreadPool 管理 IO 工作线程以及对应的 EventLoop。采用 one loop per thread 模型：
// 1. mainLoop 运行在创建 TcpServer 的线程中，负责监听连接。
// 2. subLoop 运行在独立 IO 线程中，负责处理已建立连接的 IO 事件。
// 3. 新连接通过 Round-Robin 策略分配到不同 subLoop，实现 IO 负载均衡。
// EventLoopThread 负责线程生命周期以及创建对应 EventLoop。EventLoopThreadPool 保存 EventLoopThread 的所有权，同时保存 subLoop 的非拥有引用用于事件分发。
class EventLoopThreadPool
{

    CLASS_NONCOPYABLE(EventLoopThreadPool)

public:

    using ThreadInitCallback = std::function<void(EventLoop *)>;


    EventLoopThreadPool(EventLoop *mainLoop, const std::string &name) : m_mainLoop(mainLoop), m_name(name) {}

    // 不要 delete loop，因为是一个栈对象。
    ~EventLoopThreadPool() = default;

    // 设置 IO 线程数量。必须在 start() 之前调用，否则无效。
    void setThreadNum(int numThreads) { m_numThreads = numThreads; }

    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 如果工作在多线程中，m_mainLoop（mainLoop）会默认以轮询的方式分配 Channel 给 subLoop。
    EventLoop *getNextLoop();

    // 获取所有的 EventLoop。
    std::vector<EventLoop *> getAllLoops();

    // 是否已经启动。
    bool started() const { return m_started; }

    // 获取名字。
    const std::string &name() const { return m_name; }


private:

    // 创建 EventLoopThreadPool 的主 EventLoop。通常运行在 main thread 中，负责 accept 新连接。
    EventLoop *m_mainLoop;

    // IO 线程中的 subEventLoop 列表。每个 subEventLoop 都由对应的 EventLoopThread 在线程内部创建，这里仅保存其地址用于后续 Round-Robin 分配 TcpConnection。注意：这里保存的是非拥有指针（non-owning pointer），EventLoop 的生命周期由 m_threads 中的 EventLoopThread 管理。
    std::vector<EventLoop *> m_subLoops;

    // EventLoopThread 对象列表。每个 EventLoopThread 对应一个 IO 工作线程，并负责创建和运行一个 subLoop。unique_ptr 负责管理 EventLoopThread 生命周期，保证 IO 线程持续存在。m_subLoops 中的 EventLoop 指针依赖于这里的对象存活。
    std::vector<std::unique_ptr<EventLoopThread>> m_threads;

    // 线程池名称，通常由用户指定，线程池中 EventLoopThread 名称依赖于线程池名称。
    std::string m_name;

    // 是否已经启动标志。
    bool m_started = false;

    // 线程池中线程的数量。
    int m_numThreads = 0;

    // Round-Robin 轮询调度时，下一个待分配的 EventLoop 下标。
    int m_next = 0;
};


#endif

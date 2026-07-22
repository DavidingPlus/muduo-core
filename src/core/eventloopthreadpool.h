#ifndef _MUDUO_CORE_EVENTLOOPTHREADPOOL_H_
#define _MUDUO_CORE_EVENTLOOPTHREADPOOL_H_

#include "globalmacros.h"

#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;


class EventLoopThreadPool
{

    CLASS_NONCOPYABLE(EventLoopThreadPool)

public:

    using ThreadInitCallback = std::function<void(EventLoop *)>;


    EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);

    ~EventLoopThreadPool();

    void setThreadNum(int numThreads) { m_numThreads = numThreads; }

    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 如果工作在多线程中，m_baseLoop（mainLoop）会默认以轮询的方式分配 Channel 给 subLoop。
    EventLoop *getNextLoop();

    // 获取所有的 EventLoop。
    std::vector<EventLoop *> getAllLoops();

    // 是否已经启动。
    bool started() const { return m_started; }

    // 获取名字。
    const std::string name() const { return m_name; }


private:

    // 用户使用 muduo 创建的 loop 如果线程数为 1，那直接使用用户创建的 loop，否则创建多 EventLoop。
    EventLoop *m_baseLoop;

    // 线程池名称，通常由用户指定，线程池中 EventLoopThread 名称依赖于线程池名称。
    std::string m_name;

    // 是否已经启动标志。
    bool m_started;

    // 线程池中线程的数量。
    int m_numThreads;

    // 新连接到来，所选择 EventLoop 的索引。
    int m_next;

    // IO 线程的列表。
    std::vector<std::unique_ptr<EventLoopThread>> m_threads;

    // 线程池中 EventLoop 的列表，指向的是 EventLoopThread 线程函数创建的 EventLoop 对象。
    std::vector<EventLoop *> m_loops;
};


#endif

#ifndef _MUDUO_CORE_EVENTLOOPTHREAD_H_
#define _MUDUO_CORE_EVENTLOOPTHREAD_H_

#include "thread.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

class EventLoop;


class EventLoopThread
{

    CLASS_NONCOPYABLE(EventLoopThread)

public:

    using ThreadInitCallback = std::function<void(EventLoop *)>;


    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(), const std::string &name = std::string()) : m_thread(std::bind(&EventLoopThread::threadFunc, this), name), m_callback(cb) {}

    ~EventLoopThread();

    EventLoop *startLoop();


private:

    void threadFunc();

    EventLoop *m_loop = nullptr;

    bool m_exiting = false;

    Thread m_thread;

    // 互斥锁。
    std::mutex m_mutex;

    // 条件变量。
    std::condition_variable m_cond;

    ThreadInitCallback m_callback;
};


#endif

#ifndef _MUDUO_CORE_THREAD_H_
#define _MUDUO_CORE_THREAD_H_

#include "globalmacros.h"

#include <functional>
#include <thread>
#include <memory>
#include <string>
#include <atomic>

#include <unistd.h>


class Thread
{

    CLASS_NONCOPYABLE(Thread)

public:

    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc, const std::string &name = std::string());

    ~Thread();

    void start();

    void join();

    bool started() { return m_started; }

    pid_t tid() const { return m_tid; }

    const std::string &name() const { return m_name; }

    static int numCreated() { return m_numCreated; }


private:

    void setDefaultName();

    static std::atomic_int m_numCreated;

    bool m_started;

    bool m_joined;

    std::shared_ptr<std::thread> m_thread;

    // 在线程创建时再绑定。
    pid_t m_tid;

    // 线程回调函数。
    ThreadFunc m_func;

    std::string m_name;
};


#endif

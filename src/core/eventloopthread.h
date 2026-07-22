#ifndef _MUDUO_CORE_EVENTLOOPTHREAD_H_
#define _MUDUO_CORE_EVENTLOOPTHREAD_H_

#include "thread.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

class EventLoop;


// 一个运行 EventLoop 的线程（one loop per thread）。EventLoop 自身并不会创建线程，它只是一个事件循环，调用 loop() 后便会一直阻塞，负责监听 IO 事件、处理回调，直到调用 quit() 才会退出。因此，如果希望 EventLoop 运行在独立线程中，就需要创建一个新线程，在线程内部创建 EventLoop，启动 EventLoop::loop()，将 EventLoop* 返回给其他线程，方便通过 runInLoop()/queueInLoop() 等接口向该事件循环投递任务。EventLoopThread 就是对上述流程的封装。需要注意的是：EventLoop 对象是在工作线程内部创建的（栈对象），因此 EventLoopThread 并不拥有 EventLoop，仅保存一个裸指针用于访问，其生命周期由工作线程负责。
class EventLoopThread
{

    CLASS_NONCOPYABLE(EventLoopThread)

public:

    using ThreadInitCallback = std::function<void(EventLoop *)>;


    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(), const std::string &name = std::string()) : m_thread(std::bind(&EventLoopThread::threadFunc, this), name), m_callback(cb) {}

    ~EventLoopThread();

    // 启动工作线程，并返回该线程对应的 EventLoop。
    EventLoop *startLoop();


private:

    // 工作线程入口函数。负责在线程内部创建并运行 EventLoop。
    void threadFunc();

    // 工作线程中的 EventLoop，仅保存地址，不拥有对象。
    EventLoop *m_loop = nullptr;

    // 是否正在退出。
    bool m_exiting = false;

    Thread m_thread;

    // 互斥锁。
    std::mutex m_mutex;

    // 条件变量。
    std::condition_variable m_cond;

    // EventLoop 创建完成后、进入事件循环前执行一次，可用于用户进行线程初始化（例如设置线程局部数据等）。
    ThreadInitCallback m_callback;
};


#endif

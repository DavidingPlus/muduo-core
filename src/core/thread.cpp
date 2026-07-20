#include "thread.h"

#include "currentthread.h"

#include <semaphore.h>


std::atomic_int Thread::m_numCreated(0);


// TODO
Thread::Thread(ThreadFunc func, const std::string &name)
{
}

Thread::~Thread()
{
}

void Thread::start()
{
}

void Thread::join()
{
    // C++ std::thread 中 join() 和 detach() 的区别：https://blog.nowcoder.net/n/8fcd9bb6e2e94d9596cf0a45c8e5858a
}

void Thread::setDefaultName()
{
}

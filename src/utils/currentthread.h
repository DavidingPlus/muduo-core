#ifndef _MUDUO_CORE_CURRENTTHREAD_H_
#define _MUDUO_CORE_CURRENTTHREAD_H_

#include <unistd.h>
#include <sys/syscall.h>


namespace CurrentThread
{
    // 保存 tid 缓存，因为系统调用非常耗时，拿到 tid 后将其保存。
    extern __thread int t_cachedTid;

    void cacheTid();

    inline int tid() // 内联函数只在当前文件中起作用
    {
        // __builtin_expect 是一种底层优化，此语句意思是如果还未获取 tid，进入 if 通过 cacheTid() 系统调用获取 tid。
        if (__builtin_expect(t_cachedTid == 0, 0))
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}

#endif

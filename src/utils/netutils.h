#ifndef _MUDUO_CORE_NETUTILS_H_
#define _MUDUO_CORE_NETUTILS_H_

class EventLoop;


namespace NetUtils
{

    /*
     * 创建线程之后主线程和子线程谁先运行是不确定的。通过一个 eventfd 在线程之间传递数据的好处是多个线程无需上锁就可以实现同步。eventfd 支持的最低内核版本为 Linux 2.6.27，在 2.6.26 及之前的版本也可以使用 eventfd，但是 flags 必须设置为 0。
     * 函数原型：
     *     #include <sys/eventfd.h>
     *     int eventfd(unsigned int initval, int flags);
     * 参数说明：
     *      initval：初始化计数器的值。
     *      flags：EFD_NONBLOCK,设置 socket 为非阻塞。EFD_CLOEXEC，执行 fork 的时候，在父进程中的描述符会自动关闭，子进程中的描述符保留。
     * 场景：
     *     eventfd 可以用于同一个进程之中的线程之间的通信。
     *     eventfd 还可以用于同亲缘关系的进程之间的通信。
     *     eventfd 用于不同亲缘关系的进程之间通信的话需要把 eventfd 放在几个进程共享的共享内存中（没有测试过）。
     */
    int CreateEventfd();

    // 创建一个非阻塞的 socket。
    int CreateSocketNonblocking();

    // 用于 TcpServer 和 TcpConnection 构造函数中，首先判断传入的 loop 是否有效，无效直接终止程序。
    EventLoop *CheckLoopNotNull(EventLoop *loop);

} // namespace NetUtils


#endif

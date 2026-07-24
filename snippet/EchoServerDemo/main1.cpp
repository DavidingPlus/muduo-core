#include "baseechoserver.h"


int main()
{
    // main1 只展示最基础的 echo server 用法：
    // 1. 创建主 EventLoop。
    // 2. 监听固定端口。
    // 3. 使用带默认回显行为的 BaseEchoServer。

    EventLoop loop;
    InetAddress addr(8080);
    BaseEchoServer server(&loop, addr, "EchoServer");

    server.start();

    // 进入事件循环后，accept/read/write 等网络事件才会真正被处理。
    loop.loop();
}

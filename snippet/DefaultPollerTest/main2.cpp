#include <iostream>

#include "poller.h"


int main()
{
    std::cout << reinterpret_cast<void *>(Poller::newDefaultPoller(nullptr)) << std::endl; // 不为空。
}

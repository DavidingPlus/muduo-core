#include <iostream>

#include "poller.h"


int main()
{
    std::cout << reinterpret_cast<void *>(Poller::NewDefaultPoller(nullptr)) << std::endl; // 不为空。
}

#include <iostream>

#include "logger.h"


int main()
{
    LOG_INFO("plain info message");
    LOG_DEBUG("plain debug message");
    LOG_ERROR("plain error message");
    LOG_FATAL("plain fatal message");
}

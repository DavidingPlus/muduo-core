#include "logger.h"

#include "logcolor.h"
#include "timestamp.h"

#include <iostream>


Logger &Logger::instance()
{
    // 详见：https://github.com/DavidingPlus/coroutine-lib/blob/master/snippet/Singleton/singleton1.cpp
    static Logger logger;
    return logger;
}

void Logger::log(const std::string &msg)
{
    std::string level;

    switch (m_logLevel)
    {
        case LogLevel::INFO:
            level = "[ INFO ]";
            break;
        case LogLevel::ERROR:
            level = "[ ERROR ]";
            break;
        case LogLevel::FATAL:
            level = "[ FATAL ]";
            break;
        case LogLevel::DEBUG:
            level = "[ DEBUG ]";
            break;
        default:
            break;
    }

    {
        LogColorGuard guard(m_logLevel);
        std::cout << level + '(' + Timestamp::now().toString() + "): ";
    }

    std::cout << msg << std::endl;
}

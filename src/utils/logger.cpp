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

void Logger::log(LogLevel level, const std::string &msg)
{
    std::string levelText;

    switch (level)
    {
        case LogLevel::INFO:
            levelText = "[ INFO ]";
            break;
        case LogLevel::ERROR:
            levelText = "[ ERROR ]";
            break;
        case LogLevel::FATAL:
            levelText = "[ FATAL ]";
            break;
        case LogLevel::DEBUG:
            levelText = "[ DEBUG ]";
            break;
        default:
            break;
    }

    {
        LogColorGuard guard(level);
        std::cout << levelText + '[' + Timestamp::now().toString() + "]";
    }

    std::cout << ' ' << msg << std::endl;
}

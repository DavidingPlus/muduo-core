#include "logger.h"


Logger &Logger::instance()
{
    // 详见：https://github.com/DavidingPlus/coroutine-lib/blob/master/snippet/Singleton/singleton1.cpp
    static Logger logger;
    return logger;
}

void Logger::log(const std::string &msg)
{
    std::string pre;

    switch (m_logLevel)
    {
        case LogLevel::INFO:
            pre = "[INFO]";
            break;
        case LogLevel::ERROR:
            pre = "[ERROR]";
            break;
        case LogLevel::FATAL:
            pre = "[FATAL]";
            break;
        case LogLevel::DEBUG:
            pre = "[DEBUG]";
            break;
        default:
            break;
    }

    // TODO
}

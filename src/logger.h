#ifndef _MUDUO_CORE_LOGGER_H_
#define _MUDUO_CORE_LOGGER_H_

#include "globalmacros.h"

#include "config.h"

#include <string>
#include <format>


#define LOG_INFO(fmt, ...)                         \
    do                                             \
    {                                              \
        Logger &logger = Logger::instance();       \
        logger.log(LogLevel::INFO,                 \
                   std::format(fmt, __VA_ARGS__)); \
    } while (0)

#define LOG_ERROR(fmt, ...)                        \
    do                                             \
    {                                              \
        Logger &logger = Logger::instance();       \
        logger.log(LogLevel::ERROR,                \
                   std::format(fmt, __VA_ARGS__)); \
    } while (0)

#define LOG_FATAL(fmt, ...)                        \
    do                                             \
    {                                              \
        Logger &logger = Logger::instance();       \
        logger.log(LogLevel::FATAL,                \
                   std::format(fmt, __VA_ARGS__)); \
    } while (0)

#ifdef MUDUO_CORE_CONFIG_DEBUG
#define LOG_DEBUG(fmt, ...)                        \
    do                                             \
    {                                              \
        Logger &logger = Logger::instance();       \
        logger.log(LogLevel::DEBUG,                \
                   std::format(fmt, __VA_ARGS__)); \
    } while (0)
#else
#define LOG_DEBUG(fmt, ...)
#endif


// 定义日志的级别。
enum class LogLevel
{
    INFO,  // 普通信息。
    ERROR, // 错误信息。
    FATAL, // core dump 信息。
    DEBUG  // 调试信息。
};


class Logger
{

    CLASS_NONCOPYABLE(Logger)

public:

    // 获取日志唯一的单例实例对象。
    // 采用内部静态变量的懒汉实现。详见：https://github.com/DavidingPlus/coroutine-lib/blob/master/snippet/Singleton/singleton1.h
    static Logger &instance();

    // 写日志。[级别信息](time): msg
    void log(LogLevel level, const std::string &msg);


protected:

    Logger() = default;

    virtual ~Logger() = default;
};


#endif

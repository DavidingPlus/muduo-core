#ifndef _MUDUO_CORE_LOGGER_H_
#define _MUDUO_CORE_LOGGER_H_

#include "globalmacros.h"

#include "config.h"

#include <string>
#include <utility>

#include <fmt/format.h>


#define LOG_INFO(...)                                     \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::Instance();              \
        logger.logFormatted(LogLevel::INFO, __VA_ARGS__); \
    } while (0)

// ERROR：表示程序运行过程中发生了错误，但该错误通常不会导致程序立即失效。调用方仍然可以根据情况进行恢复，例如重试、降级处理或者忽略。示例：网络连接失败、文件读取失败等。
#define LOG_ERROR(...)                                     \
    do                                                     \
    {                                                      \
        Logger &logger = Logger::Instance();               \
        logger.logFormatted(LogLevel::ERROR, __VA_ARGS__); \
    } while (0)

// FATAL：表示发生了无法恢复的致命错误，程序已经无法继续保证正确运行。输出日志后应该立即终止程序（通常调用 abort()），方便定位问题。示例：核心组件初始化失败、内存结构损坏、关键资源创建失败等。
#define LOG_FATAL(...)                                     \
    do                                                     \
    {                                                      \
        Logger &logger = Logger::Instance();               \
        logger.logFormatted(LogLevel::FATAL, __VA_ARGS__); \
        std::abort();                                      \
    } while (0)

#if MUDUO_CORE_CONFIG_DEBUG
#define LOG_DEBUG(...)                                     \
    do                                                     \
    {                                                      \
        Logger &logger = Logger::Instance();               \
        logger.logFormatted(LogLevel::DEBUG, __VA_ARGS__); \
    } while (0)
#else
#define LOG_DEBUG(...)
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
    static Logger &Instance();

    // 写日志。[级别信息](time): msg
    void log(LogLevel level, const std::string &msg);

    // LOG 宏统一转到这里做 fmt 格式化，避免在宏里直接处理可变参数，
    template <typename... Args>
    void logFormatted(LogLevel level, fmt::format_string<Args...> format, Args &&...args) { log(level, fmt::format(format, std::forward<Args>(args)...)); }


protected:

    Logger() = default;

    virtual ~Logger() = default;
};


#endif

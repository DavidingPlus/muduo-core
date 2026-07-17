#ifndef _MUDUO_CORE_LOGGER_H_
#define _MUDUO_CORE_LOGGER_H_

#include "globalmacros.h"

#include <string>


class Logger
{

    CLASS_NONCOPYABLE(Logger)

public:

    // 定义日志的级别。
    enum class LogLevel
    {
        INFO,  // 普通信息。
        ERROR, // 错误信息。
        FATAL, // core dump 信息。
        DEBUG  // 调试信息。
    };


    // 获取日志唯一的单例实例对象。
    static Logger &instance();

    // 设置日志级别。
    void setLogLevel(int level);

    // 写日志。
    void log(std::string msg);


protected:

    Logger() : m_logLevel(LogLevel::INFO) {}

    virtual ~Logger() = default;


private:

    LogLevel m_logLevel;
};


#endif

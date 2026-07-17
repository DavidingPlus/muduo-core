#ifndef _MUDUO_CORE_LOGCOLOR_H_
#define _MUDUO_CORE_LOGCOLOR_H_

#include "logger.h"


// 清除所有颜色，恢复到默认。
#define LOG_COLOR_RESET "\033[0m"

// 普通信息，绿色。
#define LOG_COLOR_INFO "\033[32m"

// 错误信息，红色。
#define LOG_COLOR_ERROR "\033[31m"

// 致命错误，亮红色。
#define LOG_COLOR_FATAL "\033[91m"

// 调试信息，蓝色。
#define LOG_COLOR_DEBUG "\033[34m"


class LogColorGuard
{

public:

    // 构造时应用新传入等级对应的颜色。
    explicit LogColorGuard(Logger::LogLevel level);

    // 析构时自动恢复原始颜色。
    ~LogColorGuard();
};


#endif

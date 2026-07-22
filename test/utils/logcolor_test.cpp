#include <gtest/gtest.h>

#include <functional>
#include <sstream>
#include <iostream>

#include "logcolor.h"
#include "logger.h"


// 这组测试同样通过捕获 cout，观察颜色控制码的成对输出。
class LogColorGuardTest : public testing::Test
{

protected:

    // 颜色守卫会直接往 cout 写控制码，这里把它们收集起来断言。
    std::string captureOutput(std::function<void()> func)
    {
        auto oldBuffer = std::cout.rdbuf();

        std::ostringstream oss;

        std::cout.rdbuf(oss.rdbuf());

        func();

        std::cout.rdbuf(oldBuffer);


        return oss.str();
    }
};


// 验证 INFO 等级会输出对应颜色序列并在析构时复位。
TEST_F(LogColorGuardTest, InfoColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::INFO); });

    EXPECT_EQ(output, std::string(LOG_COLOR_INFO) + LOG_COLOR_RESET);
}

// 验证 ERROR 等级会输出对应颜色序列并在析构时复位。
TEST_F(LogColorGuardTest, ErrorColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::ERROR); });

    EXPECT_EQ(output, std::string(LOG_COLOR_ERROR) + LOG_COLOR_RESET);
}

// 验证 FATAL 等级会输出对应颜色序列并在析构时复位。
TEST_F(LogColorGuardTest, FatalColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::FATAL); });

    EXPECT_EQ(output, std::string(LOG_COLOR_FATAL) + LOG_COLOR_RESET);
}

// 验证 DEBUG 等级会输出对应颜色序列并在析构时复位。
TEST_F(LogColorGuardTest, DebugColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::DEBUG); });

    EXPECT_EQ(output, std::string(LOG_COLOR_DEBUG) + LOG_COLOR_RESET);
}

// 验证 LogColorGuard 用 RAII 方式在构造/析构时成对输出颜色控制码。
TEST_F(LogColorGuardTest, RAIIOrder)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::INFO); });

    // 构造时输出 INFO 颜色，析构时输出 RESET。
    EXPECT_EQ(output, std::string(LOG_COLOR_INFO) + LOG_COLOR_RESET);
}

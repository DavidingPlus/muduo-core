#include <gtest/gtest.h>

#include <functional>
#include <sstream>
#include <iostream>

#include "logcolor.h"
#include "logger.h"


class LogColorGuardTest : public testing::Test
{

protected:

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


TEST_F(LogColorGuardTest, InfoColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::INFO); });

    EXPECT_EQ(output, std::string(LOG_COLOR_INFO) + LOG_COLOR_RESET);
}

TEST_F(LogColorGuardTest, ErrorColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::ERROR); });

    EXPECT_EQ(output, std::string(LOG_COLOR_ERROR) + LOG_COLOR_RESET);
}

TEST_F(LogColorGuardTest, FatalColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::FATAL); });

    EXPECT_EQ(output, std::string(LOG_COLOR_FATAL) + LOG_COLOR_RESET);
}

TEST_F(LogColorGuardTest, DebugColor)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::DEBUG); });

    EXPECT_EQ(output, std::string(LOG_COLOR_DEBUG) + LOG_COLOR_RESET);
}

TEST_F(LogColorGuardTest, RAIIOrder)
{
    auto output = captureOutput([]()
                                { LogColorGuard guard(LogLevel::INFO); });

    // 构造时输出 INFO 颜色，析构时输出 RESET。
    EXPECT_EQ(output, std::string(LOG_COLOR_INFO) + LOG_COLOR_RESET);
}

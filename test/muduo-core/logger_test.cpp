#include <gtest/gtest.h>

#include <iostream>
#include <sstream>

#include "logger.h"


class LoggerTest : public testing::Test
{

protected:

    // SetUp() 和 TearDown() 函数类似 unity 单元测试框架那个。

    // 每个测试开始前恢复默认日志级别。
    void SetUp() override { Logger::instance().setLogLevel(Logger::LogLevel::INFO); }

    void TearDown() override {}

    // 捕获 std::cout 输出。
    std::string captureOutput(std::function<void()> func)
    {
        // 使用 std::cout.rdbuf() 保存原来的 cout buffer。
        auto oldBuffer = std::cout.rdbuf();
        // 创建字符串缓冲区。
        std::ostringstream oss;
        // 将 cout 重定向到 oss 代表的字符串缓冲区。
        std::cout.rdbuf(oss.rdbuf());
        // 执行日志输出。
        func();
        // 恢复 cout。
        std::cout.rdbuf(oldBuffer);


        // 返回捕获内容。
        return oss.str();
    }
};


TEST_F(LoggerTest, Singleton)
{
    Logger &logger1 = Logger::instance();
    Logger &logger2 = Logger::instance();

    // 两次获取应该是同一个对象。
    EXPECT_EQ(&logger1, &logger2);
}

TEST_F(LoggerTest, DefaultLevel)
{
    auto output = captureOutput([]()
                                { Logger::instance().log("hello"); });

    EXPECT_NE(output.find("[INFO]"), std::string::npos);
    EXPECT_NE(output.find("hello"), std::string::npos);
}

TEST_F(LoggerTest, InfoLevel)
{
    Logger::instance().setLogLevel(Logger::LogLevel::INFO);
    auto output = captureOutput([]()
                                { Logger::instance().log("info message"); });

    EXPECT_NE(output.find("[INFO]"), std::string::npos);
    EXPECT_NE(output.find("info message"), std::string::npos);
}

TEST_F(LoggerTest, ErrorLevel)
{
    Logger::instance().setLogLevel(Logger::LogLevel::ERROR);
    auto output = captureOutput([]()
                                { Logger::instance().log("error message"); });

    EXPECT_NE(output.find("[ERROR]"), std::string::npos);
    EXPECT_NE(output.find("error message"), std::string::npos);
}

TEST_F(LoggerTest, FatalLevel)
{
    Logger::instance().setLogLevel(Logger::LogLevel::FATAL);
    auto output = captureOutput([]()
                                { Logger::instance().log("fatal message"); });


    EXPECT_NE(output.find("[FATAL]"), std::string::npos);
    EXPECT_NE(output.find("fatal message"), std::string::npos);
}

TEST_F(LoggerTest, DebugLevel)
{
    Logger::instance().setLogLevel(Logger::LogLevel::DEBUG);
    auto output = captureOutput([]()
                                { Logger::instance().log("debug message"); });

    EXPECT_NE(output.find("[DEBUG]"), std::string::npos);
    EXPECT_NE(output.find("debug message"), std::string::npos);
}

TEST_F(LoggerTest, TimestampFormat)
{
    Logger::instance().log("timestamp test");

    auto output = captureOutput([]()
                                { Logger::instance().log("timestamp test"); });

    /*
     * Logger 输出格式：
     *
     * [INFO] 2026-07-17 16:20:30.123456000 : timestamp test
     *
     * 检查：
     *
     * 1. 是否存在日期分隔符 '/'
     * 2. 是否存在时间分隔符 ':'
     * 3. 是否存在微秒 '.'
     */
    EXPECT_NE(output.find("/"), std::string::npos);
    EXPECT_NE(output.find(":"), std::string::npos);
    EXPECT_NE(output.find("."), std::string::npos);
}

TEST_F(LoggerTest, EmptyMessage)
{
    auto output = captureOutput([]()
                                { Logger::instance().log(""); });

    EXPECT_NE(output.find("[INFO]"), std::string::npos);
}

TEST_F(LoggerTest, SpecialCharacters)
{
    std::string msg = "hello\nworld\t123";
    auto output = captureOutput([&]()
                                { Logger::instance().log(msg); });

    EXPECT_NE(output.find("hello"), std::string::npos);
    EXPECT_NE(output.find("world"), std::string::npos);
    EXPECT_NE(output.find("123"), std::string::npos);
}

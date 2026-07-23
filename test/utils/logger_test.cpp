#include <gtest/gtest.h>

#include <iostream>
#include <sstream>

#include "logger.h"


// 这组测试通过重定向 std::cout 来断言 Logger 的最终输出。
class LoggerTest : public testing::Test
{

protected:

    // 把 std::cout 重定向到字符串流里，方便断言日志内容。
    std::string captureOutput(std::function<void()> func)
    {
        // 先保存原来的 buffer，测试结束后再恢复。
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


// 验证 Logger::Instance() 返回的是单例。
TEST_F(LoggerTest, Singleton)
{
    Logger &logger1 = Logger::Instance();
    Logger &logger2 = Logger::Instance();

    // 两次获取应该是同一个对象。
    EXPECT_EQ(&logger1, &logger2);
}

// 验证默认 INFO 级别日志会带上正确的标签和消息。
TEST_F(LoggerTest, DefaultLevel)
{
    Logger::Instance().log(LogLevel::INFO, "hello");
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::INFO, "hello"); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
    EXPECT_NE(output.find("hello"), std::string::npos);
}

// 验证 INFO 级别的输出格式。
TEST_F(LoggerTest, InfoLevel)
{
    Logger::Instance().log(LogLevel::INFO, "info message");
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::INFO, "info message"); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
    EXPECT_NE(output.find("info message"), std::string::npos);
}

// 验证 ERROR 级别的输出格式。
TEST_F(LoggerTest, ErrorLevel)
{
    Logger::Instance().log(LogLevel::ERROR, "error message");
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::ERROR, "error message"); });

    EXPECT_NE(output.find("[ ERROR ]"), std::string::npos);
    EXPECT_NE(output.find("error message"), std::string::npos);
}

// 验证 FATAL 级别的输出格式。
TEST_F(LoggerTest, FatalLevel)
{
    Logger::Instance().log(LogLevel::FATAL, "fatal message");
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::FATAL, "fatal message"); });


    EXPECT_NE(output.find("[ FATAL ]"), std::string::npos);
    EXPECT_NE(output.find("fatal message"), std::string::npos);
}

// 验证 DEBUG 级别的输出格式。
TEST_F(LoggerTest, DebugLevel)
{
    Logger::Instance().log(LogLevel::DEBUG, "debug message");
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::DEBUG, "debug message"); });

    EXPECT_NE(output.find("[ DEBUG ]"), std::string::npos);
    EXPECT_NE(output.find("debug message"), std::string::npos);
}

// 验证日志里会打印日期、时间和微秒字段。
TEST_F(LoggerTest, TimestampFormat)
{
    Logger::Instance().log(LogLevel::INFO, "timestamp test");
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::INFO, "timestamp test"); });

    /*
     * Logger 输出格式：
     *
     * [ INFO ] 2026-07-17 16:20:30.123456000 : timestamp test
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

// 验证空消息也会正常输出等级前缀。
TEST_F(LoggerTest, EmptyMessage)
{
    auto output = captureOutput([]()
                                { Logger::Instance().log(LogLevel::INFO, ""); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
}

// 验证消息里带换行和制表符时也能正常输出。
TEST_F(LoggerTest, SpecialCharacters)
{
    std::string msg = "hello\nworld\t123";
    auto output = captureOutput([&]()
                                { Logger::Instance().log(LogLevel::INFO, msg); });

    EXPECT_NE(output.find("hello"), std::string::npos);
    EXPECT_NE(output.find("world"), std::string::npos);
    EXPECT_NE(output.find("123"), std::string::npos);
}

// 验证 LOG_INFO 宏会把参数格式化后输出。
TEST_F(LoggerTest, LogInfoMacro)
{
    LOG_INFO("info {}", 123);
    auto output = captureOutput([]()
                                { LOG_INFO("info {}", 123); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
    EXPECT_NE(output.find("info 123"), std::string::npos);
}

// 验证不带可变参数时，LOG_INFO 也能正常展开。
TEST_F(LoggerTest, LogInfoMacroWithoutVariadicArgs)
{
    LOG_INFO("plain info message");
    auto output = captureOutput([]()
                                { LOG_INFO("plain info message"); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
    EXPECT_NE(output.find("plain info message"), std::string::npos);
}

// 验证 LOG_INFO 能正确格式化普通指针。
TEST_F(LoggerTest, LogInfoMacroWithPointer)
{
    int value = 42;

    LOG_INFO("pointer {}", fmt::ptr(&value));

    auto expectedPtr = fmt::format("{}", fmt::ptr(&value));
    auto output = captureOutput([&]()
                                { LOG_INFO("pointer {}", fmt::ptr(&value)); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
    EXPECT_NE(output.find(expectedPtr), std::string::npos);
}

// 验证 LOG_INFO 能正确格式化空指针。
TEST_F(LoggerTest, LogInfoMacroWithNullPointer)
{
    int *ptr = nullptr;

    LOG_INFO("pointer {}", fmt::ptr(ptr));

    auto expectedPtr = fmt::format("{}", fmt::ptr(ptr));
    auto output = captureOutput([&]()
                                { LOG_INFO("pointer {}", fmt::ptr(ptr)); });

    EXPECT_NE(output.find("[ INFO ]"), std::string::npos);
    EXPECT_NE(output.find(expectedPtr), std::string::npos);
}

// 验证 LOG_ERROR 宏的输出内容。
TEST_F(LoggerTest, LogErrorMacro)
{
    LOG_ERROR("error {}", 456);
    auto output = captureOutput([]()
                                { LOG_ERROR("error {}", 456); });

    EXPECT_NE(output.find("[ ERROR ]"), std::string::npos);
    EXPECT_NE(output.find("error 456"), std::string::npos);
}

// 验证不带参数时，LOG_ERROR 也能正常输出。
TEST_F(LoggerTest, LogErrorMacroWithoutVariadicArgs)
{
    LOG_ERROR("plain error message");
    auto output = captureOutput([]()
                                { LOG_ERROR("plain error message"); });

    EXPECT_NE(output.find("[ ERROR ]"), std::string::npos);
    EXPECT_NE(output.find("plain error message"), std::string::npos);
}

// FATAL 会触发终止流程，所以这里用 DISABLED_ 只验证格式，不在默认跑。
TEST_F(LoggerTest, DISABLED_LogFatalMacro)
{
    LOG_FATAL("fatal {}", 789);
    auto output = captureOutput([]()
                                { LOG_FATAL("fatal {}", 789); });

    EXPECT_NE(output.find("[ FATAL ]"), std::string::npos);
    EXPECT_NE(output.find("fatal 789"), std::string::npos);
}

// 同上，验证无参数 FATAL 宏的格式但默认不执行。
TEST_F(LoggerTest, DISABLED_LogFatalMacroWithoutVariadicArgs)
{
    LOG_FATAL("plain fatal message");
    auto output = captureOutput([]()
                                { LOG_FATAL("plain fatal message"); });

    EXPECT_NE(output.find("[ FATAL ]"), std::string::npos);
    EXPECT_NE(output.find("plain fatal message"), std::string::npos);
}

// 验证 DEBUG 宏在调试构建下会输出，发布构建下会被静默。
TEST_F(LoggerTest, LogDebugMacro)
{
    LOG_DEBUG("debug {}", 321);
    auto output = captureOutput([]()
                                { LOG_DEBUG("debug {}", 321); });

#if MUDUO_CORE_CONFIG_DEBUG
    EXPECT_NE(output.find("[ DEBUG ]"), std::string::npos);
    EXPECT_NE(output.find("debug 321"), std::string::npos);
#else
    EXPECT_TRUE(output.empty());
#endif
}

// 验证无参数 DEBUG 宏在不同构建配置下的行为。
TEST_F(LoggerTest, LogDebugMacroWithoutVariadicArgs)
{
    LOG_DEBUG("plain debug message");
    auto output = captureOutput([]()
                                { LOG_DEBUG("plain debug message"); });

#if MUDUO_CORE_CONFIG_DEBUG
    EXPECT_NE(output.find("[ DEBUG ]"), std::string::npos);
    EXPECT_NE(output.find("plain debug message"), std::string::npos);
#else
    EXPECT_TRUE(output.empty());
#endif
}

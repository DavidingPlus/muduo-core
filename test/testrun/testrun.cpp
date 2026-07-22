#include <gtest/gtest.h>


// 最基础的冒烟测试，确认测试二进制本身能正常运行。
TEST(TestRunTests, TestRun)
{
    SUCCEED();
}

// 以 DISABLED_ 开头的测试会被 GTest 统计到 disabled，但默认不会执行。这里故意写成 FAIL()，用来证明它不会影响最终通过结果。
TEST(TestRunTests, DISABLED_ExampleDisabledTest)
{
    FAIL();
}

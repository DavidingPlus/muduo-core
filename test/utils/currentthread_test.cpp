#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <tuple>

#include "currentthread.h"


class CurrentThreadTest : public testing::Test
{

protected:

    void SetUp() override
    {
        // t_cachedTid 是当前测试线程上的 thread_local 缓存，Google Test 在主线程上跑这个测试，前一个用例调用 tid()/cacheTid() 后留下的值不会自动恢复。这里显式清零，保证每个测试都从“尚未缓存 tid”开始。
        CurrentThread::t_cachedTid = 0;
    }

    void TearDown() override
    {
        // 测试结束后也恢复，避免当前用例的缓存状态泄漏到后续用例。
        CurrentThread::t_cachedTid = 0;
    }
};


TEST_F(CurrentThreadTest, TidReturnsKernelThreadId)
{
    const int tid = CurrentThread::tid();

    EXPECT_EQ(tid, static_cast<int>(::gettid()));
    EXPECT_EQ(CurrentThread::t_cachedTid, tid);
}

TEST_F(CurrentThreadTest, TidPopulatesCacheOnFirstCall)
{
    ASSERT_EQ(CurrentThread::t_cachedTid, 0);

    const int first = CurrentThread::tid();

    EXPECT_NE(first, 0);
    EXPECT_EQ(CurrentThread::t_cachedTid, first);
}

TEST_F(CurrentThreadTest, TidReturnsCachedValueOnSubsequentCalls)
{
    const int first = CurrentThread::tid();
    ASSERT_NE(first, 0);

    CurrentThread::t_cachedTid = 123456;

    const int second = CurrentThread::tid();

    EXPECT_EQ(second, 123456);
}

TEST_F(CurrentThreadTest, CacheTidInitializesCacheExplicitly)
{
    ASSERT_EQ(CurrentThread::t_cachedTid, 0);

    CurrentThread::cacheTid();

    EXPECT_EQ(CurrentThread::t_cachedTid, static_cast<int>(::gettid()));
    EXPECT_EQ(CurrentThread::tid(), CurrentThread::t_cachedTid);
}

TEST_F(CurrentThreadTest, CacheTidDoesNotOverwriteExistingCache)
{
    CurrentThread::t_cachedTid = 424242;

    CurrentThread::cacheTid();

    EXPECT_EQ(CurrentThread::t_cachedTid, 424242);
}

TEST_F(CurrentThreadTest, DifferentThreadsKeepIndependentCaches)
{
    const int mainTid = CurrentThread::tid();
    ASSERT_NE(mainTid, 0);

    std::promise<std::tuple<int, int, int>> promise;
    auto future = promise.get_future();

    std::thread worker([&promise]()
                       {
                           const int initialCache = CurrentThread::t_cachedTid;
                           const int firstTid = CurrentThread::tid();
                           const int secondTid = CurrentThread::tid();
                           promise.set_value({initialCache, firstTid, secondTid}); });

    const auto [initialCache, workerTid, workerTidAgain] = future.get();
    worker.join();

    EXPECT_EQ(initialCache, 0);
    EXPECT_NE(workerTid, 0);
    EXPECT_EQ(workerTidAgain, workerTid);
    EXPECT_NE(workerTid, mainTid);
    EXPECT_EQ(CurrentThread::tid(), mainTid);
    EXPECT_EQ(CurrentThread::t_cachedTid, mainTid);
}

#include "timestamp.h"

#include <chrono>
#include <format>


Timestamp Timestamp::now()
{
    // 获取当前系统时间。
    // 注意，因为是要获取绝对时间，因此不能使用只单调递增的 steady_clock，因为系统的绝对时间可能改变。
    auto now = std::chrono::system_clock::now();
    // 获取从 Unix epoch (1970-01-01 00:00:00 UTC) 到当前时间经过的微秒数。
    auto microSeconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();


    return Timestamp(microSeconds);
}

std::string Timestamp::toString() const
{
    // 将保存的微秒时间戳转换成 system_clock::time_point。
    auto tp = std::chrono::system_clock::time_point(std::chrono::microseconds(m_microSecondsSinceEpoch));
    // 转换成本地时间。
    auto localTime = std::chrono::current_zone()->to_local(tp);


    // 格式化日期时间。
    return std::format("{:%Y/%m/%d %H:%M:%S}", localTime);
}

#ifndef _MUDUO_CORE_TIMESTAMP_H_
#define _MUDUO_CORE_TIMESTAMP_H_

#include <string>


class Timestamp
{

public:

    Timestamp() : m_microSecondsSinceEpoch(static_cast<int64_t>(0)) {}

    explicit Timestamp(int64_t microSecondsSinceEpoch) : m_microSecondsSinceEpoch(microSecondsSinceEpoch) {}

    ~Timestamp() = default;

    int64_t microSecondsSinceEpoch() const { return m_microSecondsSinceEpoch; }

    // 获取当前系统时间戳。
    static Timestamp now();

    std::string toString() const;


private:

    int64_t m_microSecondsSinceEpoch;
};


#endif

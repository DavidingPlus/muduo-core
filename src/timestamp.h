#ifndef _MUDUO_CORE_TIMESTAMP_H_
#define _MUDUO_CORE_TIMESTAMP_H_

#include <string>


class Timestamp
{

public:

    Timestamp() : m_microSecondsSinceEpoch(0) {}

    explicit Timestamp(int64_t microSecondsSinceEpoch) : m_microSecondsSinceEpoch(m_microSecondsSinceEpoch) {}

    virtual ~Timestamp() = default;

    // 获取当前系统时间戳。
    static Timestamp now();

    std::string toString() const;


private:

    int64_t m_microSecondsSinceEpoch;
};


#endif

#ifndef _MUDUO_CORE_GLOBALMACROS_H_
#define _MUDUO_CORE_GLOBALMACROS_H_


#define CLASS_NONCOPYABLE(ClassName)                       \
                                                           \
private:                                                   \
                                                           \
    ClassName(const ClassName &other) = delete;            \
    ClassName(ClassName &&other) = delete;                 \
    ClassName &operator=(const ClassName &other) = delete; \
    ClassName &operator=(ClassName &&other) = delete;


#endif

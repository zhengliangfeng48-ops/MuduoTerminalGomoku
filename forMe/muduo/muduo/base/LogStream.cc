#include "muduo/base/LogStream.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

using namespace muduo;
using namespace muduo::detail;

// TODO: better itoa.
// 跨编译器屏蔽“恒真/恒假比较”类警告
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wtautological-compare"
#else
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

namespace muduo{

namespace detail{

const char digits[] = "9876543210123456789";
const char* zero = digits + 9;
static_assert(sizeof(digits) == 20, "wrong number of digits");
// size 包括末尾的'\0'

const char digitsHex[] = "0123456789ABCDEF";    // hex:十六进制
static_assert(sizeof digitsHex == 17, "wrong number of digitsHex");

// Efficient Integer to String Conversions, by Matthew Wilson.
template<typename T>
size_t convert(char buf[], T value){
    T i = value;
    char* p = buf;

    do{
        int lsd = static_cast<int>(i % 10);
        i /= 10;
        *p++ = zero[lsd];
    } while (i != 0);

    if (value < 0){
        *p++ = '-';
    }
    *p = '\0';
    std::reverse(buf, p);   // std::reverse 左闭右开

    return p - buf;
}

// 十六进制的转换传参为无符号整型
size_t convertHex(char buf[], uintptr_t value){
    uintptr_t i = value;
    char* p = buf;

    do{
        int lsd = static_cast<int>(i % 16);
        i /= 16;
        *p++ = digitsHex[lsd];
    } while (i != 0);

    *p = '\0';
    std::reverse(buf, p);

    return p - buf;
}

template class FixedBuffer<kSmallBuffer>;
template class FixedBuffer<kLargeBuffer>;

}   // namespace detail

/*
 Format a number with 5 characters, including SI units.
 [0,     999]
 [1.00k, 999k]
 [1.00M, 999M]
 [1.00G, 999G]
 [1.00T, 999T]
 [1.00P, 999P]
 [1.00E, inf)
 保持三位有效数字
*/
std::string formatSI(int64_t s){
    double n = static_cast<double>(s);
    char buf[64];
    if (s < 1000)
        snprintf(buf, sizeof(buf), "%" PRId64, s);
    else if (s < 9995)
        snprintf(buf, sizeof(buf), "%.2fk", n/1e3);
    else if (s < 99950)
        snprintf(buf, sizeof(buf), "%.1fk", n/1e3);
    else if (s < 999500)
        snprintf(buf, sizeof(buf), "%.0fk", n/1e3);
    else if (s < 9995000)
        snprintf(buf, sizeof(buf), "%.2fM", n/1e6);
    else if (s < 99950000)
        snprintf(buf, sizeof(buf), "%.1fM", n/1e6);
    else if (s < 999500000)
        snprintf(buf, sizeof(buf), "%.0fM", n/1e6);
    else if (s < 9995000000)
        snprintf(buf, sizeof(buf), "%.2fG", n/1e9);
    else if (s < 99950000000)
        snprintf(buf, sizeof(buf), "%.1fG", n/1e9);
    else if (s < 999500000000)
        snprintf(buf, sizeof(buf), "%.0fG", n/1e9);
    else if (s < 9995000000000)
        snprintf(buf, sizeof(buf), "%.2fT", n/1e12);
    else if (s < 99950000000000)
        snprintf(buf, sizeof(buf), "%.1fT", n/1e12);
    else if (s < 999500000000000)
        snprintf(buf, sizeof(buf), "%.0fT", n/1e12);
    else if (s < 9995000000000000)
        snprintf(buf, sizeof(buf), "%.2fP", n/1e15);
    else if (s < 99950000000000000)
        snprintf(buf, sizeof(buf), "%.1fP", n/1e15);
    else if (s < 999500000000000000)
        snprintf(buf, sizeof(buf), "%.0fP", n/1e15);
    else
        snprintf(buf, sizeof(buf), "%.2fE", n/1e18);
    return buf;
}

/*
 [0, 1023]
 [1.00Ki, 9.99Ki]
 [10.0Ki, 99.9Ki]
 [ 100Ki, 1023Ki]
 [1.00Mi, 9.99Mi]
*/
std::string formatIEC(int64_t s){
    double n = static_cast<double>(s);
    char buf[64];
    const double Ki = 1024.0;
    const double Mi = Ki * 1024.0;
    const double Gi = Mi * 1024.0;
    const double Ti = Gi * 1024.0;
    const double Pi = Ti * 1024.0;
    const double Ei = Pi * 1024.0;

    if (n < Ki)
        snprintf(buf, sizeof buf, "%" PRId64, s);
    else if (n < Ki*9.995)
        snprintf(buf, sizeof buf, "%.2fKi", n / Ki);
    else if (n < Ki*99.95)
        snprintf(buf, sizeof buf, "%.1fKi", n / Ki);
    else if (n < Ki*1023.5)
        snprintf(buf, sizeof buf, "%.0fKi", n / Ki);

    else if (n < Mi*9.995)
        snprintf(buf, sizeof buf, "%.2fMi", n / Mi);
    else if (n < Mi*99.95)
        snprintf(buf, sizeof buf, "%.1fMi", n / Mi);
    else if (n < Mi*1023.5)
        snprintf(buf, sizeof buf, "%.0fMi", n / Mi);

    else if (n < Gi*9.995)
        snprintf(buf, sizeof buf, "%.2fGi", n / Gi);
    else if (n < Gi*99.95)
        snprintf(buf, sizeof buf, "%.1fGi", n / Gi);
    else if (n < Gi*1023.5)
        snprintf(buf, sizeof buf, "%.0fGi", n / Gi);

    else if (n < Ti*9.995)
        snprintf(buf, sizeof buf, "%.2fTi", n / Ti);
    else if (n < Ti*99.95)
        snprintf(buf, sizeof buf, "%.1fTi", n / Ti);
    else if (n < Ti*1023.5)
        snprintf(buf, sizeof buf, "%.0fTi", n / Ti);

    else if (n < Pi*9.995)
        snprintf(buf, sizeof buf, "%.2fPi", n / Pi);
    else if (n < Pi*99.95)
        snprintf(buf, sizeof buf, "%.1fPi", n / Pi);
    else if (n < Pi*1023.5)
        snprintf(buf, sizeof buf, "%.0fPi", n / Pi);

    else if (n < Ei*9.995)
        snprintf(buf, sizeof buf, "%.2fEi", n / Ei );
    else
        snprintf(buf, sizeof buf, "%.1fEi", n / Ei );
    return buf;
}

}   // namespace muduo

template<int SIZE>
const char* FixedBuffer<SIZE>::debugString(){
    *cur_ = '\0';
    return data_;
}

/*
它们永远不会被执行，存在的意义只是提供两个地址。
构造时把 cookieStart 的地址存进成员 cookie_，
析构时换成 cookieEnd。这是给 GDB 看的一种“调试标记”，
空实现是故意的
*/
template<int SIZE>
void FixedBuffer<SIZE>::cookieStart(){}

template<int SIZE>
void FixedBuffer<SIZE>::cookieEnd(){}

// 从不调用
void LogStream::staticCheck(){
    // numeric_limits<T>::digits10 是类型 T 能无损打印的十进制有效位数
    // 预留10字节给负号、单位、'\0'等字符
    static_assert(kMaxNumericSize - 10 > std::numeric_limits<double>::digits10,
                    "kMaxNumericSize is large enough");
    static_assert(kMaxNumericSize - 10 > std::numeric_limits<long double>::digits10,
                    "kMaxNumericSize is large enough");
    static_assert(kMaxNumericSize - 10 > std::numeric_limits<long>::digits10,
                    "kMaxNumericSize is large enough");
    static_assert(kMaxNumericSize - 10 > std::numeric_limits<long long>::digits10,
                    "kMaxNumericSize is large enough");
}

template<typename T>
void LogStream::formatInteger(T v){
    if (buffer_.avail() >= kMaxNumericSize){
        size_t len = convert(buffer_.current(), v);
        buffer_.add(len);
    }
}

LogStream& LogStream::operator<<(short v){
    *this << static_cast<int>(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned short v){
    *this << static_cast<unsigned int>(v);
    return *this;
}

LogStream& LogStream::operator<<(int v){
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned int v){
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(long v){
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long v){
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(long long v){
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long long v){
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(const void* p){
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    if (buffer_.avail() >= kMaxNumericSize){
        char* buf = buffer_.current();
        buf[0] = '0';
        buf[1] = 'x';
        size_t len = convertHex(buf+2, v);
        buffer_.add(len+2);
    }
    return *this;
}

// FIXME: replace this with Grisu3 by Florian Loitsch.
LogStream& LogStream::operator<<(double v){
    if (buffer_.avail() >= kMaxNumericSize){
        // 12位有效数字
        int len = snprintf(buffer_.current(), kMaxNumericSize, "%.12g", v);
        buffer_.add(len);
    }
    return *this;
}

template<typename T>
Fmt::Fmt(const char* fmt, T val){
    // 断言T是算术类型（整型或浮点型）
    static_assert(std::is_arithmetic<T>::value == true, "Must be arithmetic type");

    length_ = snprintf(buf_, sizeof buf_, fmt, val);
    assert(static_cast<size_t>(length_) < sizeof buf_); // < 32
}

// Explicit instantiations
/*
显式实例化,指在代码中明确地告诉编译器为特定的模板参数生成模板的实例
（即具体的类或函数代码），而不是等待编译器在首次使用时隐式生成。
*/
template Fmt::Fmt(const char* fmt, char);

template Fmt::Fmt(const char* fmt, short);
template Fmt::Fmt(const char* fmt, unsigned short);
template Fmt::Fmt(const char* fmt, int);
template Fmt::Fmt(const char* fmt, unsigned int);
template Fmt::Fmt(const char* fmt, long);
template Fmt::Fmt(const char* fmt, unsigned long);
template Fmt::Fmt(const char* fmt, long long);
template Fmt::Fmt(const char* fmt, unsigned long long);

template Fmt::Fmt(const char* fmt, float);
template Fmt::Fmt(const char* fmt, double);

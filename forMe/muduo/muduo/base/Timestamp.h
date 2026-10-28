#ifndef MUDUO_BASE_TIMESTAMP_H
#define MUDUO_BASE_TIMESTAMP_H

#include "muduo/base/copyable.h"
#include "muduo/base/Types.h"

#include<boost/operators.hpp>

namespace muduo{

// 时间戳以 UTC 为准，分辨率为微秒。
// 这个类是不可变的。
// 建议通过值传递它，因为在x64架构上它是通过寄存器传递的 
class Timestamp:public muduo::copyable,
                public boost::equality_comparable<Timestamp>,
                public boost::less_than_comparable<Timestamp>
{   //以上两个boost的类继承后定义 operator== 和 operator<, 
    //就会自动补全 operator!=、operator>、operator<=、operator>=

public:
    /// Constucts an invalid Timestamp.
    Timestamp():microSecondsSinceEpoch_(0){}
    
    // 以下“///”和“@param”仅仅用于Doxygen风格的工具解析生成API文档
    // explicit关键字: 禁止编译器利用该函数进行隐式类型转换
    /// Constucts a Timestamp at specific time
    /// @param microSecondsSinceEpoch
    explicit Timestamp(int64_t microSecondsSinceEpochArg)
        :microSecondsSinceEpoch_(microSecondsSinceEpochArg){}

    void swap(Timestamp& that){
        std::swap(microSecondsSinceEpoch_,that.microSecondsSinceEpoch_);
    }

    // default copy/assignment/dtor are Okay 

    string toString() const;
    string toFormattedString(bool showMicroseconds=true) const;

    bool valid() const{ return microSecondsSinceEpoch_ > 0; }

    int64_t microSecondsSinceEpoch() const{
         return microSecondsSinceEpoch_; 
    }    

    time_t secondsSinceEpoch() const {
        return static_cast<time_t>(microSecondsSinceEpoch_/kMicroSecondsPerSecond);
    }

    static Timestamp now();

    static Timestamp invalid(){
        return Timestamp();
    }

    static Timestamp fromUnixTime(time_t t){
        return fromUnixTime(t,0);
    }

    static Timestamp fromUnixTime(time_t t,int microseconds){
        return Timestamp(static_cast<int64_t>(t)*kMicroSecondsPerSecond+microseconds);
    }

    static const int kMicroSecondsPerSecond=1000*1000;

private:
    int64_t microSecondsSinceEpoch_;
};

inline bool operator<(Timestamp lhs,Timestamp rhs){
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs,Timestamp rhs){
        return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

/// Gets time difference of two timestamps, result in seconds.
///
/// @param high, low
/// @return (high-low) in seconds
/// @c double has 52-bit precision, enough for one-microsecond
/// resolution for next 100 years.
inline double timeDifference(Timestamp high,Timestamp low){
    int64_t diff=high.microSecondsSinceEpoch()-low.microSecondsSinceEpoch();
    return static_cast<double>(diff)/Timestamp::kMicroSecondsPerSecond;
}

/// Add @c seconds to given timestamp.
/// @return timestamp+seconds as Timestamp
inline Timestamp addTime(Timestamp timestamp,double seconds){
    int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
    return Timestamp(timestamp.microSecondsSinceEpoch()+delta);
}

}   // namespace muduo 

#endif  //MUDUO_BASE_TIMESTAMP_H 
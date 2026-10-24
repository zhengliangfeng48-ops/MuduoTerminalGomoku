#ifndef MUDUO_BASE_ATOMIC_H
#define MUDUO_BASE_ATOMIC_H

#include "muduo/base/noncopyable.h"
#include <stdint.h>

namespace muduo{

namespace detail{

template<typename T>
class AtomicIntegerT:noncopyable{
public:
    AtomicIntegerT():value_(0){}

    //uncomment if you need copying and assignment 
    /*
    AtomicIntegerT(const AtomicIntegerT& that)
        : value_(that.get()){}
    
    AtomicIntegerT& operator=(const AtomicIntegerT& that){
        getAndSet(that.get());
        return *this;
    }
    */

    T get(){
        //纯粹的原子加载函数，直接返回value的值
        return _atomic_load_n(&value_,__ATOMIC_SEQ_CST);
    }

    T getAndAdd(T x){
        //原子地将 x 加到 value_ 上，并返回加之前 value_ 的旧值
        return __atomic_fetch_add(&value_,x,__ATOMIC_SEQ_CST);
    }

    T addAndGet(T x){
        return getAndAdd(x)+x;
    }

    T incrementAndGet(){
        return addAndGet(1);
    }

    T decrementAndGet(){
        return addAndGet(-1);
    }

    void add(T x){
        getAndAdd(x);
    }

    void increment(){
        incrementAndGet();
    }

    void decrement(){
        decrementAndGet();
    }

    T getAndSet(T newValue){
        //把 newValue 写入 &value_，并返回写入之前的旧值
        return __atomic_exchange_n(&value_,newValue,__ATOMIC_SEQ_CST);
    }

private:
    volatile T value_;
};

}   //namespace detail

typedef detail::AtomicIntegerT<int32_t> AtomicInt32;
typedef detail::AtomicIntegetT<int64_t> AtomicInt64;

}   //namespace muduo

#endif  //MUDUO_BASE_ATOMIC_H
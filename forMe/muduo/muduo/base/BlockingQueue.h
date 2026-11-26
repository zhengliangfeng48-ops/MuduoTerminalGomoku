#ifndef MUDUO_BASE_BLOCKINGQUEUE_H
#define MUDUO_BASE_BLOCKINGQUEUE_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

#include <deque>
#include <assert.h>

namespace muduo{

template<typename T>
class BlockingQueue : noncopyable{
public:
    using queue_type = std::deque<T>;

    BlockingQueue()
        : mutex_(),
        notEmpty_(mutex_),
        queue_(){}

    void put(const T& x){
        MutexLockGuard lock(mutex_);
        queue_.push_back(x);
        notEmpty_.notify(); // wait morphing saves us
        /* wait morphing: 持锁 notify 时，等待者会被直接挂到 mutex 等待队列上，
            而不是先唤醒再抢锁，避免“通知丢失”和多余的上下文切换        */
    }

    void put(T&& x){
        MutexLockGuard lock(mutex_);
        queue_.push_back(std::move(x)); // 按传入参数的类型继续传右值
        notEmpty_.notify();
    }

    T take(){
        MutexLockGuard lock(mutex_);
        // always use a while-loop, due to spurious(虚假的) wakeup
        while (queue_.empty()){
            notEmpty_.wait();
        }
        assert(!queue_.empty());
        T front(std::move(queue_.front())); // 无移动构造则退化为拷贝构造
        queue_.pop_front();
        return front;
    }

    queue_type drain(){
        std::deque<T> queue;
        {
            MutexLockGuard lock(mutex_);
            queue = std::move(queue_);
            assert(queue_.empty());
        }
        return queue;
    }

    size_t size() const{
        MutexLockGuard lock(mutex_);
        return queue_.size();
    }

private:
    mutable MutexLock mutex_;
    Condition         notEmpty_ GUARDED_BY(mutex_);
    queue_type        queue_ GUARDED_BY(mutex_);

};  // __attribute__ ((aligned (64)));
// 强制指定类的内存起始地址对齐到 64 字节边界

}   // namespace muduo

#endif  // MUDUO_BASE_BLOCKINGQUEUE_H
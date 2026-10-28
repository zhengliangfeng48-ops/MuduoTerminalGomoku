#ifndef MUDUO_BASE_MUTEX_H
#define MUDUO_BASE_MUTEX_H

#include "muduo/base/CurrentThread.h"
#include "muduo/base/noncopyable.h"
#include <assert.h>
#include <pthread.h>

// Thread safety annotations {
// https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
// 仅使用clang启用线程安全属性。
// 在使用其他编译器编译时，这些属性可以被安全地擦除

// 只有当前编译器是 Clang，并且没有定义 SWIG 时，才启用线程安全属性
#if defined(__clang__) && (!defined(SWIG))
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   // no-op
#endif

// 把某个类型标记为一种“能力”
#define CAPABILITY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

// 标记 RAII 风格的锁对象，例如类似 std::lock_guard 的类
// 告诉 Clang：该对象构造时获取能力，析构时释放能力
#define SCOPED_CAPABILITY \
    THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

// 声明某个变量/成员必须持有能力 x 时才能访问
#define GUARDED_BY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

// 类似 GUARDED_BY，但用于指针指向的数据受 x 保护，PT 表示 pointer
#define PT_GUARDED_BY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

// 声明锁的获取顺序，表示某些能力必须在另一些能力之前获取
#define ACQUIRED_BEFORE(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

// 声明某些能力必须在另一些能力之后获取
#define ACQUIRED_AFTER(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

// 修饰函数，表示调用该函数前，当前线程必须独占持有指定能力
#define REQUIRES(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

// 表示调用前必须共享持有指定能力
#define REQUIRES_SHARED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

// 修饰函数，表示该函数会获取指定能力
#define ACQUIRE(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

// 表示函数会获取共享能力，通常对应读锁
#define ACQUIRE_SHARED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

// 表示函数会释放指定能力
#define RELEASE(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

// 表示函数会释放共享能力
#define RELEASE_SHARED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

// 修饰 try_lock() 一类函数，表示尝试获取能力，成功与否取决于返回值
#define TRY_ACQUIRE(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

// 尝试获取共享能力，通常对应 try_lock_shared()
#define TRY_ACQUIRE_SHARED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

// 修饰函数，表示调用该函数时不能持有指定能力
#define EXCLUDES(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

// 断言告诉 Clang 静态分析器：执行到此处时，当前线程一定持有能力 x
#define ASSERT_CAPABILITY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

// 断言当前线程一定共享持有能力 x
#define ASSERT_SHARED_CAPABILITY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

// 修饰函数，表示函数返回了一个与能力 x 相关的锁对象或引用
#define RETURN_CAPABILITY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

// 修饰函数，表示对该函数关闭 Clang 线程安全分析
#define NO_THREAD_SAFETY_ANALYSIS \
    THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// End of thread safety annotations }

#ifdef CHECK_PTHREAD_RETURN_VALUE

// 这段代码来自C标准库的<assert.h>头文件
#ifdef NDEBUG
__BEGIN_DECLS
// 以下函数由 assert_perror 宏在断言失败时调用，不抛异常、无返回值
extern void __assert_perror_fail (int errnum,
                                    const char *file,
                                    unsigned int line,
                                    const char *function)
    noexcept __attribute__ ((__noreturn__));
__END_DECLS
#endif

#define MCHECK(ret) ({ __typeof__ (ret) errnum = (ret);         \
                       if (__builtin_expect(errnum != 0, 0))    \
                         __assert_perror_fail (errnum, __FILE__, __LINE__, __func__);})

#else   // CHECK_PTHREAD_RETURN_VALUE

#define MCHECK(ret) ({ __typeof__ (ret) errnum = (ret);         \
                        assert(errnum == 0); (void) errnum;})

#endif // CHECK_PTHREAD_RETURN_VALUE

namespace muduo{

/*  Use as data member of a class, eg.

class Foo{
public:
    int size() const;

private:
    mutable MutexLock mutex_;
    std::vector<int> data_ GUARDED_BY(mutex_);
};      */
class CAPABILITY("mutex") MutexLock : noncopyable {
public:
    MutexLock(): holder_(0){
        MCHECK(pthread_mutex_init(&mutex_, NULL));
    }

    ~MutexLock(){
        assert(holder_ == 0);
        MCHECK(pthread_mutex_destroy(&mutex_));
    }

    // must be called when locked, i.e. for assertion
    bool isLockedByThisThread() const {
        return holder_ == CurrentThread::tid();
    }

    void assertLocked() const ASSERT_CAPABILITY(this) {
        assert(isLockedByThisThread());
    }

    // internal usage

    void lock() ACQUIRE(){
        MCHECK(pthread_mutex_lock(&mutex_));
        assignHolder();
    }

    void unlock() RELEASE() {
        unassignHolder();
        MCHECK(pthread_mutex_unlock(&mutex_));
    }

    pthread_mutex_t* getPthreadMutex(){ // non-const 
        return &mutex_;
    }

private:
    friend class Condition;

    class UnassignGuard : noncopyable{
    public:
        explicit UnassignGuard(MutexLock& owner)
            : owner_(owner){
            owner_.unassignHolder();
        }

        ~UnassignGuard(){
            owner_.assignHolder();
        }
    
    private:
        MutexLock& owner_;
    };

    void unassignHolder(){
        holder_ = 0;
    }

    void assignHolder(){
        holder_ = CurrentThread::tid();
    }

    pthread_mutex_t mutex_;
    pid_t holder_;
};

/* Use as a stack variable, eg.

int Foo::size() const {
    MutexLockGuard lock(mutex_);
    return data_.size();
}   */
class SCOPED_CAPABILITY MutexLockGuard : noncopyable{
public:
    explicit MutexLockGuard(MutexLock& mutex) ACQUIRE(mutex)
        : mutex_(mutex) {
        mutex_.lock();
    }

    ~MutexLockGuard() RELEASE(){
        mutex_.unlock();
    }

private:
    MutexLock& mutex_;
};

}   // namespace muduo

// 防止此滥用行为: MutexLockGuard(mutex_);
// A tempory object doesn't hold the lock for long!
#define MutexLockGuard(x) error "Missing guard object name"

#endif
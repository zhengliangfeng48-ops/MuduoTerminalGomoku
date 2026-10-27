#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include "muduo/base/Types.h"

namespace muduo{

namespace CurrentThread{

    // extern:      只声明，定义位于其他文件   
    // __thread:    每个线程独占一个该变量，并非全局变量
    extern __thread int t_cachedTid;
    extern __thread char t_tidString[32];
    extern __thread int t_tidStringLength;
    extern __thread const char* t_threadName;
    void cacheTid();

    inline int tid(){
        // __builtin_expect(long a,long b) 的返回值就是第一个参数 a
        // 该函数的作用是优化条件分支的指令布局
        // 即我告诉编译器，a 的值大概率是 b
        if(__builtin_expect(t_cachedTid == 0, 0)){
            cacheTid();
        }
        return t_chchedTid;
    }

    inline const char* tidString(){ // for logging
        return t_tidString;
    }

    inline int tidStringLength(){   // for logging
        return t_tidStringLength;
    }

    inline const char* name(){
        return t_threadName;
    }

    bool isMainThread();

    void sleepUsec(int64_t usec);  // for testing

    // 堆栈跟踪，通常指程序抛出异常或错误时，
    // 系统打印出的调用栈信息，显示方法调用的层级关系
    string stackTrace(bool demangle);

}   // namespace CurrentThread

}   // namespace muduo

#endif  // MUDUO_BASE_CURRENTTHREAD_H
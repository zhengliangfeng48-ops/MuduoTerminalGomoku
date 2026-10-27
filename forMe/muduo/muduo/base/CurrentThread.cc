// .h 中声明但未定义：
// void cacheTid();
// bool isMainThread();
// void sleepUsec(int64_t usec);  // for testing

#include "muduo/base/CurrentThread.h"

#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>

namespace muduo{

namespace CurrentThread{

__thread int t_cachedTid = 0;
__thread char t_tidString[32];
__thread int t_tidStringLength = 6;
__thread const char* t_threadName = "unknown";

// is_same 为判断两类型是否相同的模板结构体，value 为 bool 成员
static_assert(std::is_same<int, pid_t>::value, "pid_t should be int");

string stackTrace(bool demangle){   // demangle: 还原
    string stack;
    const int max_frames = 200;
    void* frame[max_frames];
    /*
    int backtrace(void **buffer, int size):
    获取当前调用栈的返回地址列表并存入 buffer 数组，
    返回值是实际获取的栈帧数量
    */
    int nptrs = ::backtrace(frame, max_frames);
    /*
    char **backtrace_symbols(void *const *buffer, int size):
    将 backtrace 得到的地址数组翻译成可读的字符串数组
    返回的字符串数组需要 free
    */
    char** strings = ::backtrace_symbols(frame, nptrs);

    if(strings){
        size_t len = 256;
        char* demangled = demangle ? static_cast<char*>(::malloc(len)) : nullptr;
        for (int i = 1; i < nptrs; ++i){
            // skipping the 0-th, which is this function
            if (demangle){
                // https://panthema.net/2008/0901-stacktrace-demangled/
                // bin/exception_test(_ZN3Bar4testEv+0x79) [0x401909]
                char* left_par = nullptr;
                char* plus = nullptr;
                for (char* p = strings[i]; *p; ++p){
                    if (*p == '(')
                        left_par = p;
                    else if (*p == '+')
                        plus = p;
                }

                if (left_par && plus){
                    *plus = '\0';
                    int status = 0;

                    char* ret = abi::__cxa_demangle(left_par+1, demangled, &len, &status);
                    /*
                    传入的 left_par+1: 指向被修饰的符号名字符串的指针。
                        这个字符串必须以 _Z 开头（Itanium ABI 格式），且以 \0 结尾。
                    传入的 demangled: 用于存放还原后可读名称的缓冲区。
                        如果传入 nullptr，函数会在内部用 malloc 动态分配内存
                    传入的 &len: 指向缓冲区长度的指针。
                        如果 output_buffer 为 nullptr，这个参数会被忽略，
                        函数成功后会将还原后字符串的长度写回到 len 中。
                    传入的 &status: 指向整型变量的指针，用于返回操作的状态码。
                        这个参数必须传地址，不能是 nullptr，否则可能导致崩溃
                    返回值: char*,还原后可读函数签名字符串的首地址，以'\0'结尾。
                    */
                    
                    *plus = '+';
                    if (status == 0){   // 成功
                        demangled = ret;  // ret could be realloc()
                        stack.append(strings[i], left_par+1);
                        stack.append(demangled);
                        stack.append(plus);
                        stack.push_back('\n');
                        continue;
                    }
                }
            }
            // Fallback to mangled names
            // 如果还原失败或者不还原(!demangle),直接放入混淆信息
            stack.append(strings[i]);
            stack.push_back('\n');
        }
        free(demangled);
        free(strings);
    }
    return stack;
}

}   // namespace CurrentThread

}   // namespace muduo

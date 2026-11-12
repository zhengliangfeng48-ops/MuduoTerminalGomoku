#ifndef MUDUO_BASE_EXCEPTION_H
#define MUDUO_BASE_EXCEPTION_H

#include "muduo/base/Types.h"
#include <exception>

namespace muduo{


/*      exception 是 cpp 标准库中所有标准异常类的基类
namespace std {
    class exception {
    public:
        exception() noexcept;
        exception(const exception&) noexcept;
        exception& operator=(const exception&) noexcept;
        virtual ~exception();
        virtual const char* what() const noexcept;
    };
}                   */
class Exception:public std::exception{
public:
    Exception(string what);

    // override: 显式声明重写了基类的虚函数
    // default: 让编译器自动生成默认实现
    ~Exception() noexcept override = default;

    // default copy-ctor and operator= are okay.

    const char* what() const noexcept override{
        return message_.c_str();
    }

    const char* stackTrace() const noexcept{
        return stack_.c_str();
    }

private:
    string message_;
    string stack_;
};

}   // namespace muduo

#endif  // MUDUO_BASE_EXCEPTION_H
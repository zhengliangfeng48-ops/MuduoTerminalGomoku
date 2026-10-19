#ifndef MUDUO_BASE_TYPES_H
#define MUDUO_BASE_TYPES_H

#include <stdint.h>
#include <string.h>  // memset
#include <string>

#ifndef NDEBUG
#include <assert.h>
#endif

namespace muduo{
    
using std::string;

inline void memZero(void* p, size_t n){
    memset(p, 0, n);
}

/*
在类型层次结构中进行向上类型转换（即，将指向Foo的指针转换为指向SuperclassOfFoo的指针，或将指向Foo的指针转换为指向Foo的const指针）时，
使用implicit_cast作为static_cast或const_cast的安全版本。当你使用implicit_cast时，编译器会检查该转换是否安全。
在许多令人惊讶的情况下，当C++要求精确的类型匹配，而不是要求参数类型可转换为目标类型时，这种显式的implicit_cast是必要的。

由于From类型可以被推断出来，因此使用implicit_cast的首选语法与static_cast等相同：
implicit_cast<ToType>(expr)
implicit_cast本应成为C++标准库的一部分，但提案提交得太晚了。它未来可能会被纳入该语言中。
*/

template<typename To, typename From>
inline To implicit_cast(From const &f){     //隐式类型转换
    return f;
}

/*
当你进行上转型（即，将指针从类型Foo转换为类型SuperclassOfFoo）时，使用implicit_cast<>是可以的，因为上转型总是成功的。
但当你进行下转型（即，将指针从类型Foo转换为类型SubclassOfFoo）时，static_cast<>并不安全，因为你如何知道指针实际上是指向SubclassOfFoo类型的？
它可能指向一个裸露的Foo，或者指向类型DifferentSubclassOfFoo。因此，在进行下转型时，你应该使用这个宏。
在调试模式下，我们使用dynamic_cast<>来再次确认下转型是否合法（如果不合法，程序会崩溃）。
在正常模式下，我们则使用高效的static_cast<>。因此，在调试模式下进行测试以确保转型合法非常重要！
这是代码中唯一应该使用dynamic_cast<>的地方。特别是，你不应该为了执行RTTI（运行时类型识别）而使用dynamic_cast<>（例如像这样的代码：
    if (dynamic_cast<Subclass1>(foo)) HandleASubclass1Object(foo);
    if (dynamic_cast<Subclass2>(foo)) HandleASubclass2Object(foo);
你应该用其他方式设计代码，这样就不需要这个了。
*/

template<typename To, typename From>     // use like this: down_cast<T*>(foo);
inline To down_cast(From* f){                     //down_cast--向下转型，即 To 是 From 的子类
    //确保To是From的子类型。此测试仅用于编译时类型检查，在运行时的优化构建中不会产生额外开销，因为它将被完全优化掉。
    if (false){
        implicit_cast<From*, To>(0);
    }

/*
若定义NDEBUG，assert() 会替换为空操作
若定义GOOGLE_PROTOBUF_NO_RTTI，RTTI 将被禁用
RTTI (Runtime Type Information，运行时类型信息) 是 C++ 的一种机制，
它允许程序在运行阶段确定一个多态对象（通常通过基类指针或引用访问）的实际动态类型，而不仅仅是它在编译期声明的静态类型。
*/
#if !defined(NDEBUG) && !defined(GOOGLE_PROTOBUF_NO_RTTI)
    assert(f == NULL || dynamic_cast<To>(f) != NULL);  // RTTI: debug mode only!
#endif
    return static_cast<To>(f);
}

}   //namespace muduo

#endif
#ifndef MUDUO_BASE_COPYABLE_H
#define MUDUO_BASE_COPYABLE_H

namespace muduo{
    /*
    一个标记类强调对象是可复制的。
    空基类优化适用。
    任何从可复制类派生的类都应该是值类型。
    */
    class copyable{
    protected:
        copyable()=default;
        ~copyable()=default;
    };
}   //namespace muduo

#endif  //MUDUO_BASE_COPYABLE_H

#ifndef MUDUO_BASE_WEAKCALLBACK_H
#define MUDUO_BASE_WEAKCALLBACK_H

#include <functional>
#include <memory>

namespace muduo{

// A barely usable WeakCallback

template<typename CLASS, typename... ARGS>      //这里的CLASS和ARGS...都是回调函数参数
class WeakCallback{
public:
    WeakCallback(const std::weak_ptr<CLASS>& object,
                const std::function<void (CLASS*, ARGS...)>& function)
    : object_(object), function_(function){}

    // 默认析构函数、复制构造函数和赋值操作符都是可以的

    void operator()(ARGS&&... args) const{
        std::shared_ptr<CLASS> ptr(object_.lock());
        if (ptr){
            function_(ptr.get(), std::forward<ARGS>(args)...);
        }
        // else{
        //   LOG_TRACE << "expired";
        // }
    }

private:
    std::weak_ptr<CLASS> object_;
    std::function<void (CLASS*, ARGS...)> function_;
};

template<typename CLASS, typename... ARGS> 
WeakCallback<CLASS, ARGS...> makeWeakCallback(const std::shared_ptr<CLASS>& object,
                                            void (CLASS::*function)(ARGS...)){          //意思说functon是CLASS类里的一个函数指针
    return WeakCallback<CLASS, ARGS...>(object, function);
}

template<typename CLASS, typename... ARGS>
WeakCallback<CLASS, ARGS...> makeWeakCallback(const std::shared_ptr<CLASS>& object,
                                                void (CLASS::*function)(ARGS...) const){    //多了一个const关键字
  return WeakCallback<CLASS, ARGS...>(object, function);
}

}   //namespace muduo

#endif  //MUDUO_BASE_WEAKCALLBACK_H
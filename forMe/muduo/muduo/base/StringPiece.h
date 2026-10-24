/*
一个类似字符串的对象，它指向另一块内存。
有助于提供一个接口，使客户端能够轻松传入“const char*”或“string”。
*/

#ifndef MUDUO_BASE_STRINGPIECE_H
#define MUDUO_BASE_STRINGPIECE_H

#include<string.h>
#include<iosfwd>        // for ostream forward-declaration

#include "muduo/base/Types.h"

namespace muduo{

class StringArg{
public:
    StringArg(const char* str):str_(str){}

    StringArg(const string& str):str_(str.c_str()){}

    const char* c_str() const{
        return str_;
    }

private:
    const char* str_;
};

class StringPiece{
private:
    const char* ptr_;
    int length_;

public:
    /*
    我们提供了非显式的单例构造函数，
    这样用户就可以在需要“StringPiece”的地方传入一个“const char*”或一个“string”。
    */
    StringPiece():ptr_(NULL),length_(0){}

    StringPiece(const char* str)
        :ptr_(str),length_(static_cast<int>(strlen(ptr_))){}

    StringPiece(const unsigned char* str)
        :ptr_(reinterpret_cast<const char*>(str)),
        length_(static_cast<int>(strlen(ptr_))){}

    StringPiece(const string& str)
        :ptr_(str.data()),length_(static_cast<int>(str.size())){}

    StringPiece(const char* offset,int len)
        :ptr_(offset),length_(len){}

    /*
    `data()` 函数可能会返回一个指向嵌入了 NUL（空字符）的缓冲区的指针，
    且返回的缓冲区可能以 NUL 结尾，也可能不以 NUL 结尾。
    因此，将 `data()` 传递给期望以 NUL 结尾的字符串的例程通常是一个错误。
    如果你确实需要这样做，请使用 "as_string().c_str()"。
    或者更好的做法是，修改你的例程，使其不依赖于 NUL 结尾。
    */

    const char* data() const{ return ptr_; }
    int size() const{ return length_; }
    bool empty() const{ return length_==0; }
    const char* begin() const{ return ptr_; }
    const char* end() const { return ptr_ + length_; }

    void clear(){
        ptr_ =NULL;
        length_=0;
    }

    void set(const char* buffer,int len){
        ptr_ =buffer;
        length_=len;
    }

    void set(const char* str){
        ptr_=str;
        length_ =static_cast<int>(strlen(str));
    }

    void set(const void* buffer,int len){
        ptr_ =reinterpret_cast<const char*>(buffer);
        length_=len;
    }

    char operator[](int i) const{ return ptr_[i]; }

    void remove_prefix(int n){
        ptr_+=n;
        length_-=n;
    }

    void remove_suffix(int n){
        length_-=n;
    }

    bool operator==(const StringPiece& x) const{
        return ((length_==x.length_)&&
            (memcmp(ptr_,x.ptr_,length_))==0);
    }

    bool operator!=(const StringPiece& x) const{
        return !(*this==x);
    }

                                                    //"\"逻辑行拼接(续行符)
#define STRINGPIECE_BINARY_PREDICATE(cmp,auxcmp)                        \
    bool operator cmp (const StringPiece& x) const{                     \
        int r=memcmp(ptr_,x.ptr_,length_<x.length_?length_:x.length_);  \
        return ((r auxcmp 0)||((r==0)&&(length_ cmp x.length)));        \
    }
    STRINGPIECE_BINARY_PREDICATE(<,  <);
    STRINGPIECE_BINARY_PREDICATE(<=, <);
    STRINGPIECE_BINARY_PREDICATE(>=, >);
    STRINGPIECE_BINARY_PREDICATE(>,  >);
//批量生成完四个operator函数就销毁此宏
#undef STRINGPIECE_BINARY_PREDICATE

    int compare(const StringPiece& x) const{
        int r=memcmp(ptr_,x.ptr,length_<x.length_?length_:x.length_);
        //只比较大小，不计算差距
        if(r==0){
            if(length_<x.length_) r=-1;
            else if(length_>x.length_)r=+1;
        }
        return r;
    }

    string as_string() const{
        return string(data(),size());
    }

    void CopyToString(string* target) const{
        target->assign(ptr_,length_);
    }

    //判断 this 是否以 x 为开头
    bool starts_with(const StringPiece& x) const{
        return ((length_>=x.length_)&&(memcmp(ptr_,x.ptr,x.length_)==0));
    }
};

}   //namespace muduo

/*
用于创建使用StringPiece的STL容器的函数。请记住，
StringPiece的生命周期最好短于底层字符串或char*的生命周期。
如果不是这样，那么你不能安全地将StringPiece存储到STL容器中
*/

#ifdef HAVE_TYPE_TRAITS
//这使得在某些STL实现中，vector<StringPiece>的速度非常快
template<> struct __type_traits<muduo::StringPiece> {
  typedef __true_type    has_trivial_default_constructor;
  typedef __true_type    has_trivial_copy_constructor;
  typedef __true_type    has_trivial_assignment_operator;
  typedef __true_type    has_trivial_destructor;
  typedef __true_type    is_POD_type;
};
#endif

// allow StringPiece to be logged
std::ostream& operator<<(std::ostream& o, const muduo::StringPiece& piece);
//zlf: 实际上此函数在muduo中从未实现

#endif  //MUDUO_BASE_STRINGPIECE_H

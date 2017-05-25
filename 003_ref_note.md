# boost库 ref学习 boost_1_63
- STL和Boost中的算法和函数大量使用函数对象作为判断式或谓词参数，这些参数都是传值语义，算法和函数在内部保留函数对象的拷贝并使用。
- 特殊情况下作为参数的函数对象拷贝代价过高或者不希望拷贝对象或者拷贝是不可行的。boost.ref应用代理模式，引入对象引用的包装器概念解决这个问题，为了使用ref需要包含头文件:
```
#include <boost/ref.hpp>
```
## 1. 类摘要
- ref库定义了一个很小很简单的引用类型的包装器，reference_wrapper, 
```
template<class T> class reference_wrapper
{
public:
    explicit reference_wrapper(T& t): t_(&t) {}
    operator T& () const { return *t_; }
    T& get() const { return *t_; }
    T* get_pointer() const { return t_; }
private:
    T* t_;
};
```
- reference_wrapper的构造函数接受类型T的引用类型，内部使用指针存储只想t的引用，构造出一个reference_wrapper对象，包装了引用。get()和get_pointer()这两个函数分别返回存储的引用和指针，相当于揭开对t的包装。

## 2. 基本用法
- reference_wrapper只在使用T的语境下才会执行隐式转换，其它情况下需要调用类型转换函数或者get()函数得到真正操作被包装对象。
```
int main(int argc, char *argv[])
{
    int x = 10;
    reference_wrapper<int> rw(x);
    cout<<(x==rw)<<endl;

    (int &)rw = 100;
    cout<<(100==rw)<<endl;

    reference_wrapper<int> rw2(rw);
    cout<<(rw2.get()==100)<<endl;

    string str;
    reference_wrapper<string> rws(str);
    *rws.get_pointer() = "ref";
    cout<<str<<endl;
    cout<<rws.get().size()<<endl;


    return 0;
}
```
## 3. 工厂函数
- ref库提供了两个便捷的工厂函数ref()和cref()，通过参数类型推导沟槽reference_wrapper对象。
```
reference_wrapper<T> ref(T& t);
reference_wrapper<T const> cref(T const& t);
```
- ref可以根据参数类型自动推导正确的reference_wrapper<T>对象，ref()产生类型为T，cref()产生类型为T const。
- reference_wrapper支持拷贝，因此ref()和cref()可以直接总在需要拷贝语义的函数参数中。
```
int main(int argc, char *argv[])
{
    double x = 2.12345;
    BOOST_AUTO(rw, cref(x));
    cout<<typeid(rw).name()<<endl;

    string str = "hahahahah";
    BOOST_AUTO(rws, ref(str));
    cout<<typeid(rws).name()<<endl;

    cout<<sqrt(ref(x))<<endl;
    return 0;
}
```
## 4. 操作包装
- ref库运用模版元编程技术提供两个特征类is_reference_wrapper和unwrap_reference，用于检测reference_wrapper对象。is_reference_wrapper<T>的bool成员变量value可以判断T是否为一个reference_wrapper。unwrap_reference<T>的内部定义type表明了T的真实类型，无论它是否经过reference_wrapper包装。
- unwrap_ref()利用unwrap_reference<T>直接解开reference_wrapper的包装(如果有的话)，返回被包装对象的引用。直接对一个未包装的对象使用unwrap_ref()直接返回对象自身的引用。unwrap_ref()可以安全地用在泛型代码中，而不必关心对象的包装特性，总能够正确地操作对象。
```
int main(int argc, char *argv[])
{
    vector<int> v(10, 2);
    BOOST_AUTO(rw, ref(v));
    cout<<is_reference_wrapper<BOOST_TYPEOF(rw)>::value<<endl;
    cout<<is_reference_wrapper<BOOST_TYPEOF(v)>::value<<endl;

    cout<<typeid(unwrap_reference<BOOST_TYPEOF(rw)>::type).name()<<endl;
    cout<<typeid(unwrap_reference<BOOST_TYPEOF(v)>::type).name()<<endl;

    int x = (int)2.12345;
    BOOST_AUTO(rwd, cref(x));
    cout<<is_reference_wrapper<BOOST_TYPEOF(rwd)>::value<<endl;
    cout<<is_reference_wrapper<BOOST_TYPEOF(x)>::value<<endl;

    cout<<typeid(unwrap_reference<BOOST_TYPEOF(rwd)>::type).name()<<endl;
    cout<<typeid(unwrap_reference<BOOST_TYPEOF(x)>::type).name()<<endl;

    unwrap_ref(rw).push_back(4);
    cout<<unwrap_ref(rw)[10]<<endl;
    
    string str("hahaha");
    BOOST_AUTO(rws, cref(str));
    cout<<unwrap_ref(rws)<<endl;

    return 0;
}
```
## 5. 为ref增加函数调用功能
- ref将对象包装为引用语义，降低了复制成本，使引用的行为更像对象，可以让容器安全地持有被包装的引用对象，可以称为"智能引用"
- ref库没有实现TR1的全部定义，不能提供函数调用操作operator(),无法包装一个函数对象的引用并传递给标准库算法。可以修改boost::ref库添加函数调用功能。

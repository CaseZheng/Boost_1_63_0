# boost库 bind学习 boost_1_63
- bind是C++98标准库中函数适配器bind1st/bind2nd的泛化和增强，可以适配任意的可调用对象，包含函数指针、函数引用、成员函数指针和函数对象。bind最多可以绑定9个函数参数，而且对绑定对象的要求很低，可以在没有result_type内部类型定义的情况下完成对函数对象的绑定。
- bind位于名词空间boost，使用bind组件需包含头文件<boost/bind.hpp>, 即:
```
#include <boost/bind.hpp>
using namespace boost;
```
## 1. 工作原理
- bind并不是单独的类或函数，是非常庞大的家族，依据绑定的参数个数和要绑定的调用对象类型，有数十种不同形式，名字都叫做bind，编译器会根据具体的绑定代码自动确定要使用的正确形式。
- bind接受的第一个参数必须是一个可调用对象，包括函数指针、函数引用、函数对象和成员函数指针，bind接受最多九个参数。参数数量必须与可调用对象的参数数量相等，这些参数将被传递给可调用对象作为输入参数。
- 绑定完成后，bind返回一个函数对象，内部保存了可调用对象的拷贝，具有operator(),返回值类型自动推导为可调用对象的返回值类型，在发生调用时，函数对象把之前存储的参数转发给可调用对象完成调用。
- bind占位符被定义为_1、_2、_3一直到_9，位于一个匿名名词空间。占位符可以取代bind中参数的位置，在发生函数调用时才接受真正的参数。
- bind占位符的名字代表它在调用式中的顺序，而在绑定表达式中没有顺序的要求。

## 2. 绑定普通函数
- bind可以绑定普通函数，包括函数、函数指针。
- 必须在绑定表达式中提供函数要求的所有参数，无论是真实参数还是占位符均可以。
```
//绑定普通函数
int f2(int a, int b)
{
    cout<<"a:"<<a<<endl;
    cout<<"b:"<<b<<endl;
    return a+b;
}

int f3(int a, int b, int c)
{
    return a+b+c;
}

struct f4: public std::binary_function<int,int,int>
{
    int operator()(int x, int y) const{
        return (x + y); 
    }
};

int main(int argc, char* argv[])
{
    cout<<boost::bind(f2, 1, 2)()<<endl;            //相当于f2(1, 2)
    cout<<boost::bind(f3, 1, 2, 3)()<<endl;         //相当于f3(1, 2, 3)
    
    cout<<boost::bind(f2, _1, 9)(4)<<endl;          //相当于f2(4, 4)
    cout<<bind2nd(f4(), 9)(4)<<endl;

    cout<<boost::bind(f2, _1, _2)(4, 5)<<endl;      //f2(4, 5)
    cout<<boost::bind(f2, _2, _1)(4, 5)<<endl;      //f2(5, 4)
    cout<<boost::bind(f2, _1, _1)(5, 4)<<endl;      //f2(5, 5)
    cout<<boost::bind(f3, _1, 8, _2)(5, 4)<<endl;      //f3(5, 8, 4)
    cout<<boost::bind(f3, _3, _2, _2)(1, 2, 3)<<endl;      //f3(3, 2, 2)

    typedef int (*f2_type)(int, int);
    typedef int (*f3_type)(int, int, int);

    f2_type pf2 = f2;
    f3_type pf3 = f3;
    cout<<boost::bind(pf2, 1, 2)()<<endl;            //相当于*pf2(1, 2)
    cout<<boost::bind(pf3, 1, 2, 3)()<<endl;         //相当于*pf3(1, 2, 3)
    return 0;
}
```
## 3. 绑定成员函数
- bind可以绑定类的成员函数。类的成员函数指针不能直接调用operator()，必须绑定一个对象或者函数，通过this指针调用成员函数。因此bind需要用一个占位符的位置让用户提供一个类的实例、引用或者指针，通过对象作为第一个参数来调用成员函数。所以使用成员函数最多只能绑定8个参数。
- bind可以代替标准库中的mem_fun和mem_fun_ref帮顶起，用来配合标准算法操作容器中对象。
- bind同样支持绑定虚拟成员函数，用法与非虚函数相同，虚函数的行为将由实际调用发生时的实例来决定。
```
//绑定成员函数
struct Test
{
public:
    int x;
public:
    Test():x(0){}
    Test(int _x):x(_x){}
    ~Test(){}
    int f2(int a, int b) const
    {
        cout<<"a:"<<a<<" b:"<<b<<endl;
        return a+b;
    }
    void print()
    {
        cout<<x<<" ";
    }
};

void print_test(Test &t)
{
    t.print();
}
void print_test_p(Test *t)
{
    if(NULL != t) { t->print(); }
}
int main(int argc, char* argv[])
{
    Test t;
    Test &yt = t;
    Test *pt = &t;
    cout<<boost::bind(&Test::f2, t, _1, 10)(10)<<endl;
    cout<<boost::bind(&Test::f2, yt, 9, 10)()<<endl;
    cout<<boost::bind(&Test::f2, pt, _1, _2)(20, 10)<<endl;
    cout<<boost::bind(&Test::f2, pt, _2, _1)(20, 10)<<endl;

    vector<Test> vecTest;
    vecTest.push_back(Test(1));
    vecTest.push_back(Test(2));
    vecTest.push_back(Test(3));
    vecTest.push_back(Test(4));

    for_each(vecTest.begin(), vecTest.end(), print_test);
    cout<<endl;
    for_each(vecTest.begin(), vecTest.end(), mem_fun_ref(&Test::print));    //mem_fun_ref用于容器存储对象实体时
    cout<<endl;
    for_each(vecTest.begin(), vecTest.end(), boost::bind(&Test::print, _1));
    cout<<endl;

    vector<Test*> vecTest_p;
    vecTest_p.push_back(new Test(5));
    vecTest_p.push_back(new Test(6));
    vecTest_p.push_back(new Test(7));
    for_each(vecTest_p.begin(), vecTest_p.end(), print_test_p);
    cout<<endl;
    for_each(vecTest_p.begin(), vecTest_p.end(), mem_fun(&Test::print));    //mem_fen用于容器存储对象指针时
    cout<<endl;
    for_each(vecTest_p.begin(), vecTest_p.end(), boost::bind(&Test::print, _1));
    cout<<endl;

    return 0;
}
```
## 4. 绑定成员变量
- bind可以绑定public成员变量
```
int main(int argc, char *argv[])
{
    vector<Test> v;
    v.push_back(Test(1));
    v.push_back(Test(2));
    v.push_back(Test(3));
    v.push_back(Test(4));
    for_each(v.begin(), v.end(), boost::bind(&Test::print, _1));
    cout<<endl;

    vector<int> v2(10);
    transform(v.begin(), v.end(), v2.begin(), bind(&Test::x, \_1));  //bind取出Test对象的成员变量x，transform算法调用bind表达式操作容器v，逐个吧变量填入到v2中
    for_each(v2.begin(), v2.end(), boost::bind(print_test_int, \_1));
    cout<<endl;

    typedef pair<int, string> pair_t;
    pair_t p(123, "bind");
    cout<<boost::bind(&pair_t::first, p)()<<endl;
    cout<<boost::bind(&pair_t::second, p)()<<endl;

    return 0;
}
```
## 5. 绑定函数对象
- bind可以绑定函数对象，包括标准库中的所有预定义的函数对象
- 如果函数对象有内部定义result_type,bind可以自动推导出返回值类型，如果函数对象未定义result_type,则需要用模版参数指明返回值类型。
- 标准库和boost库大部分函数对象都具有result_type定义。
```
struct f
{
    int operator()(int x, int y)
    {
        return x + y;
    }
};

struct g
{
    typedef int result_type;
    int operator()(int x, int y)
    {
        return x + y;
    }
};
//绑定函数对象
int main(int argc, char *argv[])
{
    cout<<boost::bind(greater<int>(), \_1, 10)(13)<<endl;     //标准库 具有result_type类型定义
    cout<<boost::bind<int>(f(), \_1, _2)(10, 15)<<endl;      //自定义函数对象，可以通过模版制定返回值类型
    cout<<boost::bind(g(), \_1, \_2)(15, 15)<<endl;           //自定义函数对象，定义result_type
    return 0;
}
```
## 6. 使用ref库
- bind采用拷贝的方式存储绑定对象和参数，如果函数对象或值参数很大、拷贝代价很高，或者无法拷贝，bind的使用会受到限制。因此bind可以搭配ref库，ref库包装对象的引用，让bind存储对象引用的拷贝，降低拷贝的代价。
- 使用ref传对象引用时，必须保证bind被调用时引用时有效的。
```
//使用ref库
int main(int argc, char *argv[])
{
    int x = 10;
    int y = 20;
    cout<<boost::bind(greater<int>(), _1, cref(x))(13)<<endl;

    g gf;
    cout<<boost::bind(ref(gf), _1, 20)(10)<<endl;

    BOOST_AUTO(r, ref(x));

    {
        int *y = new int(5);
        r = ref(*y);
        cout<<r<<endl;
        cout<<bind(f3, r, 1, 1)()<<endl;
        delete y;
    }

    int *w = new int(8);
    cout<<bind(f3, r, 1, 1)()<<endl;

    return 0;
}
```
## 7. 高级议题
### 为占位符更名
- 最简单的方法：为原占位符使用引用创建别名，可以使用BOOST_AUTO，无需关心占位符的真实类型，将推导的工作交给编译器。
### 存储bind表达式
- bind表达式生成的函数对象类型声明非常复杂，可以使用typedef库BOOST_AUTO宏辅助，也可以用function库。
### 嵌套绑定
- bind可以嵌套，一个bind表达式生成的函数对象可以被另一个bind再绑定，实现类似f(g(x))的形式。bind嵌套要特别小心，不容易写正确和理解。
### 操作符重载
- bind重载了比较操作符和逻辑非操作符,可以将多个bind绑定组合起来，形成复杂的逻辑表达式，配合标准库实现语法简单但语义复杂的操作。
### 绑定非标准函数
- 有的非标准函数bind无法推导出返回值，必须显式地指定bind的返回值类型。
- bind不支持使用了不同调用方式(如__stdcall、__fastcall、extern "C")的函数，bind将它们看做函数对象，需要显式指定bind的返回值类型。或者在<boost/bind.hpp>之前加上BOOST_BIND_ENABLE_STDCALL、BOOST_BIND_ENABLE_FASTCALL、BOOST_BIND_ENABLE_PASCAL等宏，明确告诉bind支持这些调用。

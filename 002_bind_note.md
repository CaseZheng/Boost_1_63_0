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

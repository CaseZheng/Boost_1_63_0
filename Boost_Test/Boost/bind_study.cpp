#include "public.h"

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

int f3(int a, int b, int c)
{
    return a+b+c;
}

void print_test(Test &t)
{
    t.print();
}
void print_test_p(Test *t)
{
    if(NULL != t) { t->print(); }
}

void print_test_int(int &t)
{
    cout<<t<<" ";
}

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

#if 1
//高级议题
int main(int argc, char *argv[])
{
    const boost::arg<1> &arg1 = _1;
    const boost::arg<2> &arg2 = _2;
    cout<<bind(f3, arg1, 3, arg2)(1, 3)<<endl;

    const BOOST_AUTO(&_x, _1);
    const BOOST_AUTO(&_y, _2);
    cout<<bind(f3, _x, 3, _y)(1, 3)<<endl;

    BOOST_AUTO(bf3, bind(f3, arg1, arg2, 5));
    cout<<bf3(5, 5)<<endl;

    cout<<bind(f3, bf3(3, 3), 1, 2)()<<endl;

    return 0;
}
#endif 

#if 0
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
#endif 

#if 0
//绑定函数对象
int main(int argc, char *argv[])
{
    cout<<boost::bind(greater<int>(), _1, 10)(13)<<endl;    //标准库 具有result_type类型定义
    cout<<boost::bind<int>(f(), _1, _2)(10, 15)<<endl;      //自定义函数对象，可以通过模版制定返回值类型
    cout<<boost::bind(g(), _1, _2)(15, 15)<<endl;           //自定义函数对象，定义result_type
    return 0;
}
#endif 

#if 0
//绑定成员变量
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
    transform(v.begin(), v.end(), v2.begin(), bind(&Test::x, _1));  //bind取出Test对象的成员变量x，transform算法调用bind表达式操作容器v，逐个吧变量填入到v2中
    for_each(v2.begin(), v2.end(), boost::bind(print_test_int, _1));
    cout<<endl;

    typedef pair<int, string> pair_t;
    pair_t p(123, "bind");
    cout<<boost::bind(&pair_t::first, p)()<<endl;
    cout<<boost::bind(&pair_t::second, p)()<<endl;

    return 0;
}
#endif

#if 0
//绑定成员函数

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
#endif

#if 0
//绑定普通函数
int f2(int a, int b)
{
    cout<<"a:"<<a<<endl;
    cout<<"b:"<<b<<endl;
    return a+b;
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
#endif

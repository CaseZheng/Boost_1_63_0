#include "public.h"

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

    return 0;
}

#include "public.h"

//异步定时器
static void handle(const boost::system::error_code &e)     //异步定时器回调函数 asio库要求回调函数只能有一个参数，而且必须是const asio::error_code &类型。
{
    cout<<time(0)<<endl;
    cout<<"timer over"<<endl;
    cout<<e<<endl;
}

void print_t()
{
    cout<<"print"<<endl;
}

class timer_test
{
private:
    int count;
    int count_max;
    function<void()> f;
    deadline_timer t;
public:
    template<typename F>
    timer_test(io_service &ios, int x, F func):
        f(func), count_max(x), count(0),t(ios, posix_time::millisec(500))
    {
        t.async_wait(bind(&timer_test::call_func, this, boost::asio::placeholders::error));
    }

    void call_func(const system::error_code &)
    {
        if(count >= count_max)
        {
            return;
        }
        ++count;
        f();
        t.expires_at(t.expires_at() + posix_time::millisec(500));
        t.async_wait(bind(&timer_test::call_func, this, boost::asio::placeholders::error()));
    }
};

int main(int argc, char *argv[])
{
    io_service service;             //所有asio程序必须有一个io_service对象

    cout<<time(0)<<endl;

    deadline_timer t(service,               //定时器，io_service作为构造函数的参数。
            boost::posix_time::seconds(2)); //2s后定时器终止。
    cout<<t.expires_at()<<endl;             //查看定时器停止时的绝对时间

    t.async_wait(handle);                   //调用wait()异步等待，传入回调函数。

    timer_test tt(service, 6, print_t);

    service.run();                  //启动前摄器的事件处理循环，阻塞等待所有的操作完成并分派事件。

    cout<<"not wait"<<endl;
    return 0;
}

#if 0
//同步定时器
int main(int argc, char *argv[])
{
    io_service service;             //所有asio程序必须有一个io_service对象

    cout<<time(0)<<endl;

    deadline_timer t(service,               //定时器，io_service作为构造函数的参数。
            boost::posix_time::seconds(2)); //2s后定时器终止。
    cout<<t.expires_at()<<endl;             //查看定时器停止时的绝对时间

    t.wait();                               //调用wait()同步等待。

    cout<<time(0)<<endl;
    cout<<"timer over"<<endl;
    
    return 0;
}
#endif

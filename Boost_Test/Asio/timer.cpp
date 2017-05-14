#include "public.h"

//异步定时器
static void handle(const boost::system::error_code &e)     //异步定时器回调函数
{
    cout<<time(0)<<endl;
    cout<<"timer over"<<endl;
    cout<<e<<endl;
}

int main(int argc, char *argv[])
{
    io_service service;             //所有asio程序必须有一个io_service对象

    cout<<time(0)<<endl;

    deadline_timer t(service,               //定时器，io_service作为构造函数的参数。
            boost::posix_time::seconds(2)); //2s后定时器终止。
    cout<<t.expires_at()<<endl;             //查看定时器停止时的绝对时间

    t.async_wait(handle);                   //调用wait()同步等待。

    service.run();

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

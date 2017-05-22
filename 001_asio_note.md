# boost库 asio学习 boost_1_63

# 高性能服务器程序框架 基础知识复习 （书籍 Linux高性能服务器编程）
- 按照服务器一般原理，可将服务器解构为以下三个主要模块：
    1. I/O处理单元。
    2. 逻辑单元。
    3. 存储单元。

## 1 服务器模型
### 1.1 C/S模型
- C/S(客户端/服务器)模型：所有客户端都通过访问服务器来获取所需的资源。
![TCP服务器和TCP客户端的工作流程](./Picture/asio_CS.jpg)
- C/S模型适合资源相对集中的场合，其实现也很简单，缺点明显：服务器是通信的中心，当访问量很大时，所有客户都讲得到很慢的响应。
### 1.2 P2P模型
- P2P(点对点)模型摒弃了以服务器为中心的格局，让网络上所有主机重新回归对等的地位。P2P模型让每台机器在消耗服务的同时也在给别人提供服务，让资源充分、自由地共享。缺点：当用户之间传输的请求过多时，网络的负载将加重。P2P模式通常带有一个专门的发现服务器，提供查找服务甚至提供内容服务，让每个客户尽快地找到自己需要的资源。P2P模型可以看做C/S模型的扩展：每台主机既是客户端，也是服务器。
![P2P模型 带有发现服务器的P2P模型](./Picture/asio_P2P.jpg)

## 2 服务器编程框架
- 服务器基本框架都一样，不同在于逻辑处理。
![服务器基本框架](./Picture/asio_service_1.jpg)

![服务器基本模块功能描述](./Picture/asio_service_2.jpg)
- I/O处理单元是服务器管理客户连接的模块。主要工作：1.等待并接受新的客户连接;2.接收客户数据;3.将服务器响应的数据返回客户端。对于服务器集群，I/O处理单元是一个专门的接入服务器，实现负载均衡，在所有逻辑服务器中选取负荷最小的一台为新客户服务。
- 一个逻辑单元是一个进程或线程，主要工作：分析并处理客户数据，并将结果传递给I/O处理单元或者直接发送给客户端。对服务器集群，一个逻辑单元就是一台逻辑服务器，服务器通常拥有多个逻辑单元，实现对多个客户任务的并行处理。
- 网络存储单元可以是数据库、缓存和文件，甚至一台独立服务器，但它不是必须的。
- 请求队列是各个单元间通信方式的抽象。对服务器集群，请求队列是各台服务器之间预先建立的、静态的、永久的TCP连接，该TCP连接提高服务器间交换数据的效率，避免动态建立TCP连接的额外的系统开销。

## 3 I/O模型
- 阻塞I/O，系统调用可能无法立即完成而被操作系统挂起，直到等待的事件发生为止。socket在创建时默认是阻塞的。通过给socket系统调用第二个参数传递SOCK_NONBLOCK标志，或通过fcntl系统调用的F_SETFL命令，可将socket设置为非阻塞的。对非阻塞I/O的系统调用总是立即返回，而不管事件是否已经发生。非阻塞I/O只有在事件已经发生的情况下操作，才能提高程序效率，因此非阻塞I/O通常和其它I/O通知机制一起使用。
- I/O复用，应用程序通过I/O复用函数向内核注册一组事件，内核通过I/O复用函数把其中就绪的事件通知给应用程序。
- SIGIO信号，为一个目标文件描述符指定宿主进程，被指定的宿主进程将捕获到SIGIO信号。当目标文件描述符上有事件发生时，SIGIO信号的信号处理函数将被触发。
- 理论上：阻塞I/O、I/O复用和信号驱动I/O都是同步I/O模型。因为在以上三种模型中，I/O的读写操作都是在I/O事件发生之后，由应用程序来完成的。
- 对于POSIX规范定义的异步I/O模型，用户可以直接对I/O执行读写操作，读写操作告诉内核用户读写缓冲区的位置，以及I/O操作完成后内核通知应用程序的方式，异步I/O的读写总是立即返回，不论I/O是否阻塞，因为真正的读写操作由内核接管。同步I/O模型要求用户代码自执行I/O操作，而异步I/O由内核来执行I/O操作。同步I/O向应用程序通知的是I/O就绪事件，而异步I/O向引用程序通知的是I/O完成事件。
![IO模型对比](./Picture/asio_IO.jpg)

## 4 两种高效的事件处理模式
- 服务器通常需要处理三类事件:I/O事件、信号及定时器事件。
- 同步I/O模型通常用于Reactor模式，异步I/O模型则用于实现Proactor模式。

### 4.1 Reactor模式
![Reactor介绍](./Picture/asio_Reactor_1.jpg)
![Reactor模式](./Picture/asio_Reactor_2.jpg)

### 4.2 Proactor模式
- Proactor模式将所有I/O操作都交给主线程和内核来处理，工作线程仅仅负责业务逻辑。因此，Proactor模式更加符合服务器基本框架。
![Proactor介绍](./Picture/asio_Proactor_1.jpg)
![Proactor模式](./Picture/asio_Proactor_2.jpg)

### 4.3 模拟Proactor模式
- 使用同步I/O方式模拟Proactor模式，原理：主线程执行数据读写操作，读写完成之后，主线程向工作线程通知这一"完成时间"。从工作线程角度，直接获得了数据读写的结果，下面只是对读写的结果进行逻辑处理。
![模拟Proactor模式](./Picture/asio_Proactor_3.jpg)

# Boost.Asio 学习
- asio库基于操作系统提供的异步机制，采用前摄器设计模式(Proactor)实现可移植的异步或者同步IO操作，并且并不要求使用多线程和锁定，有效避免多线程编程带来的诸多有害副作用(如条件竞争、死锁等)。
- Boost.Asio是一个跨平台、主要用于网络和其他一些底层输入/输出编程的C++库。
- Boost.Asio在网络通信、COM串行端口和文件上成功地抽象了输入输出的概念。
- Boost.Asio依赖如下库：
    1. Boost.System:为Boost库提供操作系统支持。
    2. Boost.Regex(可选的):以便重载Read_until()或者async_read_until()是使用boost::regex参数。
    3. Boost.DateTime(可选的):以便使用Boost.Asio中的计时器。
    4. OpenSSL(可选的):以便使用Boost.Asio提供的SSL支持。
- asio 位于名字空间boost::asio ,需要包含头文件如下:
```
#define BOOST_REGEX_NO_LIB
#define BOOST_DATE_TIME_SOURCE
#define BOOST_SYSTEM_NO_LIB
#include <boost/asio.hpp>
using namespace boost::asio;
```
## 1. 概述
- asio库基于Proactor封装了操作系统的select、poll/epoll、kqueue、overlappedI/O等机制，实现异步IO模型。
- asio的核心类是io_service, 相当于前摄器模式中的Proactor角色，asio的任何操作都需要io_service的参与。
- 同步模式下，程序发起一个I/O操作，向io_service提交请求，io_service把操作转交给操作系统，同步等待，等I/O操作完成，操作系统通知io_service，然后io_service将结果发回程序，完成整个同步流程。
- 异步模式下，程序除了发起I/O操作，还需要定义一个用于回调的完成处理函数。io_service同样把IO操作转交给操作系统，但不同步等待，而是立即返回，调用io_service的run()成员函数等待异步操作完成，当异步操作完成时io_service从操作系统获取执行结果，调用完成处理函数。
- asio不直接使用操作系统提供的线程，而是定义了一个自己的线程概念：stand，保证多线程环境中代码可以正确执行，而无需使用互斥量。io_service::stand::wrap()函数可以包装一个函数在strand中执行。
- asio专门用两个类mutable_buffer和const_buffer来封装缓存区，它们可以安全的应用到异步的读写当中,使用自由函数buffer()可以包装常用的C++容器类型(array,vector,string等),用read()、write()函数读取缓存区。
- asio使用system库的error_code和system_error表示程序运行错误。基本所有函数有两种重载，一是有error_code的输出参数，调用后检查这个参数验证错误，二是没有error_code参数，发生错误则抛出system_error异常, 调用代码用try-catch块捕获错误。

## 2. 定时器
- 定时器功能的主要类是deadline_timer
- 定时器deadline_timer有两种形式的构造函数，都要求有io_service对象，用于提交IO请求，另一个参数是posix_time的绝对时间点或者是自当前时间开始的时间长度。
- 定时器对象创建，立即开始计时，可用成员函数wait()同步等待定时器终止，或使用async_wait()异步等待，当定时器终止时会调用handler函数。
- 如果创建定时器不制定终止时间，定时器不会工作，可用成员函数expires_at()和expires_from_now()分别设定定时器终止的绝对时间和相对时间，然后调用wait()或async_wait()等待。expires_at()和expires_from_now()的无参重载形式可以获得定时器的终止时间。
- 定时器cancel()函数。通知所有异步操作取消，转而等待定时器终止。

## 3. 定时器用法
### 同步定时器
```
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
```
- io_service对象，是前摄器中最重要的proactor角色。
### 异步定时器
```
//异步定时器
static void handle(const boost::system::error_code &e)     //异步定时器回调函数 asio库要求回调函数只能有一个参数，而且必须是const asio::error_code &类型。
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

    t.async_wait(handle);                   //调用wait()异步等待，传入回调函数。

    service.run();                  //启动前摄器的事件处理循环，阻塞等待所有的操作完成并分派事件。

    cout<<"not wait"<<endl;
    return 0;
}

```
### 异步绑定器使用bind



#include "public.h"

typedef boost::shared_ptr<ip::tcp::socket> socket_ptr;

void start_accept(socket_ptr sock);
void handle_accept(socket_ptr sock, const boost::system::error_code & err);

io_service service;
ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 2001);
ip::tcp::acceptor acc(service, ep);

int main(int argc, char *argv[])
{
    //基本的异步服务器
    socket_ptr sock(new ip::tcp::socket(service));
    start_accept(sock);
    service.run();
    return 0;
}

void start_accept(socket_ptr sock)
{
    acc.async_accept(*sock, bind(handle_accept, sock));
}

void handle_accept(socket_ptr sock, const boost::system::error_code & err)
{
    cout<<err<<endl;
    if(err)
        return;
    socket_ptr sock(new ip::tcp::socket(service), err);
    start_accept(sock);
}

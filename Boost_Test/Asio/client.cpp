#include "public.h"

void connect_handler(const boost::system::error_code & ec)
{
    cout<<ec<<endl;
}

int main(int argc, char *argv[])
{
    //同步客户端
    io_service service;
    ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 2001);
    ip::tcp::socket sock(service);
    sock.async_connect(ep, connect_handler);
    service.run();
    return 0;
}


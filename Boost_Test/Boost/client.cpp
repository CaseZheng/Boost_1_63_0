#include "public.h"

#if 0
//同步客户端
int main(int argc, char *argv[])
{
    io_service service;

    //socket对象
    ip::tcp::socket sock(service);

    //连接端点
    ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), PORT);
    
    boost::system::error_code error;
    //连接服务器
    sock.connect(ep, error);

    if(error)   //出错打印下出错信息
    {
        cout<<"connect error: "<<error<<endl;
        cout<<"connect error: "<<boost::system::system_error(error).what()<<endl;
        return -1;
    }

    std::vector<char> buf(128);
    size_t len = sock.read_some(boost::asio::buffer(buf), error);
    if(error == boost::asio::error::eof)
    {
        cout<<"error eof: "<<error<<endl;
        cout<<"error eof: "<<boost::system::system_error(error).what()<<endl;
    }
    else if(error)
    {
        cout<<"error: "<<error<<endl;
        cout<<"error: "<<boost::system::system_error(error).what()<<endl;
    }
    else
    {
        cout<<string(buf.begin(), buf.end())<<endl;
        sock.write_some(boost::asio::buffer(buf, len), error);
    }
    return 0;
}
#endif

#if 0
int main(int argc, char *argv[])
{
    io_service service;

    ip::address addr;
    addr = addr.from_string("127.0.0.1");
    cout<<addr.is_v4()<<endl;
    cout<<addr.is_v6()<<endl;
    cout<<addr.to_string()<<endl;

    addr = addr.from_string("ab::12:13:14");
    cout<<addr.is_v4()<<endl;
    cout<<addr.is_v6()<<endl;
    cout<<addr.to_string()<<endl;

    ip::tcp::endpoint epp(addr, 8000);
    cout<<epp.address()<<endl;
    cout<<epp.port()<<endl;
    return 0;
}
#endif



#if 1
int main(int argc, char *argv[])
{
    io_service service;
    ip::tcp::resolver rlv(service);
    ip::tcp::tcp::endpoint endpoint;
    ip::tcp::resolver::query qry("www.google.com", "http");
    ip::tcp::resolver::iterator iter = rlv.resolve(qry);
    ip::tcp::resolver::iterator end;
    system::error_code ec = error::host_not_found;
    for(;ec && iter!=end; ++iter)
    {
        endpoint = *iter;
        cout<<endpoint.address().to_string()<<endl;
    }

    return 0;
}
#endif

#include "public.h"

using boost::asio::ip::tcp;
using boost::asio::io_service;
using boost::asio::buffer;

typedef boost::shared_ptr<ip::tcp::socket> socket_ptr;


#if 1

const int max_len = 1024;

class clientSession : public boost::enable_shared_from_this<clientSession>
{
public:
    clientSession(boost::asio::io_service &ioservice) : m_socket(ioservice)
    {
        //cout<<"ip:"<<m_socket.remote_endpoint().address()
            //<<" port:"<<m_socket.remote_endpoint().port()<<endl;
        memset(data_, '\0', sizeof(data_));
    }
    virtual ~clientSession(){}
    tcp::socket& socket()
    {
        return m_socket;
    }
    void start()
    {
        cout<<m_socket<<endl;
        boost::asio::async_write(m_socket,
            boost::asio::buffer("link successed!"),
            boost::bind(&clientSession::handle_write,shared_from_this(),
            boost::asio::placeholders::error));

        m_socket.async_read_some(boost::asio::buffer(data_,max_len),
            boost::bind(&clientSession::handle_read,shared_from_this(),
            boost::asio::placeholders::error));
    }
private:
    void handle_write(const boost::system::error_code &error)
    {
        if(error)
        {
            m_socket.close();
        }
    }
    void handle_read(const boost::system::error_code& error)
    {
        if(!error)
        {
            std::cout <<data_<< std::endl;
            m_socket.async_read_some(boost::asio::buffer(data_, max_len), 
                    boost::bind(&clientSession::handle_read, shared_from_this(),
                        boost::asio::placeholders::error));
        }
        else
        {
            m_socket.close();
        }
    }
private:
    tcp::socket m_socket;
    char data_[max_len];
};

typedef boost::shared_ptr<clientSession> session_ptr;

class serverApp
{
public:
    serverApp(boost::asio::io_service& ioservice,tcp::endpoint& endpoint) : 
        m_ioservice(ioservice),
        acceptor_(ioservice,endpoint)
    {
        session_ptr new_session(new clientSession(ioservice));
        acceptor_.async_accept(new_session->socket(),
            boost::bind(&serverApp::handle_accept, this, boost::asio::placeholders::error, 
                new_session));
    }
    virtual ~serverApp(){}
private:
    void handle_accept(const boost::system::error_code& error, session_ptr& session)
    {
        if(!error)
        {
            std::cout << "get a new client!" << std::endl;
            //实现对每个客户端的数据处理
            session->start();
            session_ptr new_session(new clientSession(m_ioservice));
            acceptor_.async_accept(new_session->socket(),
                boost::bind(&serverApp::handle_accept, this, boost::asio::placeholders::error,
                    new_session));
        }
    }
private:
    boost::asio::io_service &m_ioservice;
    tcp::acceptor acceptor_;
};

int main(int argc, char *argv[])
{
    boost::asio::io_service myIoService;
    tcp::endpoint endPoint(tcp::v4(), PORT);
    serverApp sa(myIoService, endPoint);
    myIoService.run();
    return 0;
}

#endif

#if 0
void start_accept(socket_ptr sock);
void handle_accept(socket_ptr sock, const boost::system::error_code & err);

//创建io_service实例
io_service service;
//指定监听端口 ip
ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 2001);
//创建接收器acc, 一个接受客户端连接，创建虚拟的socket,异步等待客户端连接的对象。
ip::tcp::acceptor acc(service, ep);

static socket_ptr accept_sock;

int main(int argc, char *argv[])
{
    //基本的异步服务器
    socket_ptr sock(new ip::tcp::socket(service));
    accept_sock = sock;
    //绑定sock的消息处理函数
    start_accept(sock);
    //运行异步service.run()循环。当接收到客户端连接时，handle_accept被调用。
    service.run();
    return 0;
}

void start_accept(socket_ptr sock)
{
    cout<<"bind_sock: "<<sock<<endl;
    //为sock绑定一个消息处理程序handle_accept
    acc.async_accept(*sock, boost::bind(handle_accept, sock, _1));
}

void handle_accept(socket_ptr sock, const boost::system::error_code & err)
{
    if(err)
    {
        cout<<"handle_accept err:"<<err<<endl;
        return;
    }
    cout<<"agr_sock: "<<sock<<endl;
    if(sock == accept_sock)
    {
        socket_ptr new_sock(new ip::tcp::socket(service));
        cout<<"new_sock: "<<new_sock<<endl;
        start_accept(new_sock);
        sock->write_some(boost::asio::buffer("client", 6));
        sock->close();
    }
    else
    {
        char buff[1024];
        size_t len = sock->read_some(buffer(buff));
        std::string msg(buff, len);
        cout<<"msg:"<<msg<<endl;
        sock->close();
    }
}
#endif

#if 0
//同步服务器
int main(int argc, char * argv[])
{
    //创建io_service实例
    io_service service;

    //指定监听端口 ip
    ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 2001);

    //创建接收器acc, 一个接受客户端连接，创建虚拟的socket,异步等待客户端连接的对象。
    ip::tcp::acceptor acc(service, ep);

    for(;;)
    {
        //socket对象
        ip::tcp::socket sock(service);
        //等待直到客户端连接进来
        acc.accept(sock);

        //打印下客户端信息
        cout<<"ip:"<<sock.remote_endpoint().address()
            <<" port:"<<sock.remote_endpoint().port()<<endl;

        boost::system::error_code error;
        //给客户端发送消息
        sock.write_some(buffer("hello client"), error);

        if(error)
        {
            cout<<"connect error: "<<error<<endl;
            cout<<"connect error: "<<boost::system::system_error(error).what()<<endl;
        }
        sock.close();
    }

    return 0;
}
#endif


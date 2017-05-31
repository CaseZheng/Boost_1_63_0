#include "public.h"

using boost::asio::ip::tcp;
using boost::asio::io_service;
using boost::asio::buffer;

typedef boost::shared_ptr<ip::tcp::socket> socket_ptr;

#if 0

const int max_len = 1024;

class clientSession : public boost::enable_shared_from_this<clientSession>
{
public:
    clientSession(boost::asio::io_service &ioservice) : m_socket(ioservice)
    {
        memset(data_, '\0', sizeof(data_));
    }
    virtual ~clientSession(){}
    tcp::socket& socket()
    {
        return m_socket;
    }
    void start()
    {
        //写入数据
        boost::asio::async_write(m_socket,
            boost::asio::buffer("link successed!"),
            boost::bind(&clientSession::handle_write, shared_from_this(),
                boost::asio::placeholders::error));

        //读取数据
        m_socket.async_read_some(boost::asio::buffer(data_,max_len),
            boost::bind(&clientSession::handle_read,shared_from_this(),
                boost::asio::placeholders::error));

        cout<<"ip:"<<m_socket.remote_endpoint().address()
            <<" port:"<<m_socket.remote_endpoint().port()<<endl;
    }
private:
    void handle_write(const boost::system::error_code &error)
    {
        if(error)
        {
            m_socket.close();
        }
        else
        {
            cout<<"send data_ success error: "<<error
                <<" "<<boost::system::system_error(error).what()<<endl;
        }
    }
    void handle_read(const boost::system::error_code& error)
    {
        if(error)
        {
            cout<<"read data_ fail error: "<<error
                <<" "<<boost::system::system_error(error).what()<<endl;
            m_socket.close();
        }
        else
        {
            cout<<"read data_ success error: "<<error
                <<" "<<boost::system::system_error(error).what()<<endl;
            std::cout <<data_<< std::endl;
            m_socket.async_read_some(boost::asio::buffer(data_, max_len), 
                    boost::bind(&clientSession::handle_read, shared_from_this(),
                        boost::asio::placeholders::error));
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
        //创建接收器acc, 一个接受客户端连接，创建虚拟的socket,异步等待客户端连接的对象。
        acceptor_(ioservice,endpoint)
    {
        session_ptr new_session(new clientSession(ioservice));
        //为sock绑定一个消息处理程序serverApp::handle_accept
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
    //创建io_service实例
    boost::asio::io_service myIoService;

    //指定监听端口 ip
    tcp::endpoint endPoint(tcp::v4(), PORT);
    serverApp sa(myIoService, endPoint);
    //运行异步service.run()循环。当接收到客户端连接时，serverApp::handle_accept被调用。
    myIoService.run();
    return 0;
}
#endif

#if 1
//同步服务器
int main(int argc, char * argv[])
{
    //创建io_service实例
    io_service service;

    //指定监听端口 ip
    ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 2001);

    //创建接收器acc, 一个接受客户端连接，创建虚拟的socket,异步等待客户端连接的对象。
    ip::tcp::acceptor acc(service, ep);
    cout<<acc.local_endpoint().address()<<endl;

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


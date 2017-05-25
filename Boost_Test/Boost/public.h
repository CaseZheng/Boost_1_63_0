#include <iostream>
#include <ctime>

#include <boost/asio.hpp>
#include <time.h>

#include "boost/filesystem.hpp"
#include "boost/shared_ptr.hpp"
#include <boost/asio/basic_stream_socket.hpp>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/asio/error.hpp>

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/ip/detail/endpoint.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/ref.hpp>

#include <boost/typeof/typeof.hpp>

using namespace std;
using namespace boost::asio;        //打开asio名词空间
using namespace boost;              //打开boost名词空间

const int PORT = 2002;


#include <iostream>

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

using namespace std;
using namespace boost::asio;

const int PORT = 2002;


#include <iostream>
#include "boost/asio.hpp"
#include "boost/filesystem.hpp"

#include "boost/socket/socket_exception.hpp"
#include "boost/socket/connector_socket.hpp"

#include "boost/socket/ip4.hpp"
#include "boost/socket/socketstream.hpp"
#include "boost/socket/acceptor_socket.hpp"
#include "boost/socket/socket_set.hpp"
#include "boost/socket/address_info.hpp"
#include "boost/lexical_cast.hpp"

#include "boost/shared_ptr.hpp"
#include "boost/bind.hpp

using namespace std;
using namespace boost::asio;
using namespace boost::socket; 


#include "udp_connection.h"
#include <iostream>
#include <Poco/Net/NetException.h>
#include "ds\util\string_util.h"

const unsigned int		ds::NET_MAX_UDP_PACKET_SIZE = 2000000;

namespace {

class BadIpException: public std::exception
{
  public:
    BadIpException(const std::string &ip)
    {
      mMsg = ip + " is outside of the Multicast range. Please choose an address between 224.0.0.0 and 239.255.255.255.";
    }
    const char *what() const
    {
      return mMsg.c_str();
    }
  private:
    std::string mMsg;
};

}

namespace ds
{

UdpConnection::UdpConnection(int numThreads)
  : mServer(false)
  , mInitialized(false)
  , mReceiveBufferMaxSize(0)
{
}

UdpConnection::~UdpConnection()
{
  close();
}

bool UdpConnection::initialize( bool server, const std::string &ip, const std::string &portSz )
{
  std::vector<std::string> numbers = ds::split(ip, ".");
  int value;
  ds::string_to_value(numbers.front(), value);
  if (!(value >= 224 && value <= 239) )
    throw BadIpException(ip);

  mServer = server;
  mIp = ip;
  mPort = portSz;
  try
  {
    unsigned short        port;
    ds::string_to_value(portSz, port);
    if ( mServer )
    {
		  mSocket.setReuseAddress(true);
		  mSocket.setReusePort(true);
		  mSocket.connect(Poco::Net::SocketAddress(ip, port));
		  mSocket.setBlocking(false);
		  mSocket.setSendBufferSize(ds::NET_MAX_UDP_PACKET_SIZE);
    }
    else
    {
		  mSocket = Poco::Net::MulticastSocket(Poco::Net::SocketAddress(Poco::Net::IPAddress(), port));
		  mSocket.setReuseAddress(true);
		  mSocket.setReusePort(true);
		  mSocket.joinGroup(Poco::Net::IPAddress(ip));
		  mSocket.setBlocking(false);
		  mSocket.setReceiveBufferSize(ds::NET_MAX_UDP_PACKET_SIZE);
		  mSocket.setReceiveTimeout(1000);

	    mReceiveBufferMaxSize = mSocket.getReceiveBufferSize();
      if (mReceiveBufferMaxSize <= 0) throw std::exception("UdpConnection::initialize() Couldn't determine a receive buffer size");
      if (!mReceiveBuffer.setSize(mReceiveBufferMaxSize)) throw std::exception("UdpConnection::initialize() Can't allocate receive buffer");
    }

    mInitialized = true;
    return true;
  }
  catch ( std::exception &e )
  {
    std::cout << e.what() << std::endl;
  }
  catch (...)
  {
    std::cout << "Caught unknown exception" << std::endl;
  }

  return false;
}

void UdpConnection::close()
{
  mInitialized = false;
  mServer = false;

  try
  {
    mSocket.close();
  }
  catch ( std::exception &e )
  {
    std::cout << e.what() << std::endl;
  }
}

void UdpConnection::renew() {
	const bool		server = mServer;
	close();
	initialize(server, mIp, mPort);
}


bool UdpConnection::sendMessage( const std::string &data )
{
  if ( !mInitialized )
    return false;

  return sendMessage(data.c_str(), data.size());
}

bool UdpConnection::sendMessage( const char *data, int size )
{
  if ( !mInitialized || size < 1)
    return false;

  try
  {
		const int sentAmt = mSocket.sendBytes(data, size);
    return sentAmt > 0;
  }
  catch ( Poco::Net::NetException &e)
  {
    std::cout << "UdpConnection::sendMessage() error " << e.message() << std::endl;
  }
  catch ( std::exception &e )
  {
    std::cout << e.what() << std::endl;
  }

  return false;
}

int UdpConnection::recvMessage( std::string &msg )
{
  if ( !mInitialized )
    return 0;

  try
  {
		if ( mSocket.available() <= 0 ) {
      return 0;
    }

    int size = mSocket.receiveBytes(mReceiveBuffer.data(), mReceiveBuffer.alloc());
    if (size > 0) {
      msg.assign(mReceiveBuffer.data(), size);
    }
    return size;
  }
  catch ( std::exception &e )
  {
    std::cout << e.what() << std::endl;
  }

  return 0;
}

bool UdpConnection::canRecv() const
{
  if ( !mInitialized )
    return 0;

  try
  {
		return mSocket.available() > 0;
  }
  catch ( std::exception &e )
  {
    std::cout << e.what() << std::endl;
  }

  return false;
}

bool UdpConnection::isServer() const
{
  return mServer;
}

bool UdpConnection::initialized() const
{
  return mInitialized;
}

}

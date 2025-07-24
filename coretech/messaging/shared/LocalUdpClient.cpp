/**
 * File: LocalUdpClient.cpp
 *
 * Description: Implementation of local-domain socket client class
 *
 * Copyright: Anki, inc. 2017
 *
 */

#include "coretech/messaging/shared/LocalUdpClient.h"
#include "coretech/messaging/shared/LocalUdpServer.h" // for kConnectionPacket
#include "coretech/messaging/shared/SocketUtils.h"
#include "coretech/common/shared/logging.h"

#include <iostream>

#include <assert.h>
#include <unistd.h>

// Socket buffer sizes
#define UDP_CLIENT_SNDBUFSZ (256*1024)
#define UDP_CLIENT_RCVBUFSZ (256*1024)

// Define this to enable logs
#define LOG_CHANNEL                    "LocalUdpClient"

#ifdef VICOS
// todo: restore logging when vicos toolchain is available -PRA (approved by BRC)
#undef LOG_CHANNEL
#endif

#ifdef  LOG_CHANNEL
#define LOG_ERROR(name, format, ...)   CORETECH_LOG_ERROR(name, format, ##__VA_ARGS__)
#define LOG_WARNING(name, format, ...) CORETECH_LOG_WARNING(name, format, ##__VA_ARGS__)
#define LOG_INFO(name, format, ...)    CORETECH_LOG_INFO(LOG_CHANNEL, name, format, ##__VA_ARGS__)
#define LOG_DEBUG(name, format, ...)   CORETECH_LOG_DEBUG(LOG_CHANNEL, name, format, ##__VA_ARGS__)
#else
#define LOG_ERROR(name, format, ...)   {}
#define LOG_WARNING(name, format, ...) {}
#define LOG_INFO(name, format, ...)    {}
#define LOG_DEBUG(name, format, ...)   {}
#endif

LocalUdpClient::LocalUdpClient(int sndbufsz, int rcvbufsz) :
  _sndbufsz(sndbufsz),
  _rcvbufsz(rcvbufsz),
  _socket(-1)
{
}

LocalUdpClient::LocalUdpClient() : LocalUdpClient(UDP_CLIENT_SNDBUFSZ, UDP_CLIENT_RCVBUFSZ)
{
}

LocalUdpClient::~LocalUdpClient()
{
  Disconnect();
}

bool LocalUdpClient::Connect(const std::string& sockname, const std::string & peername)
{
  //LOG_DEBUG("LocalUdpClient.Connect", "Connect from %s to %s", sockname.c_str(), peername.c_str());

  if (_socket >= 0) {
    LOG_ERROR("LocalUdpClient.Connect", "Already connected");
    return false;
  }

  const int ai_family = AF_LOCAL;
  const int ai_socktype = SOCK_DGRAM;
  const int ai_protocol = 0;

  _socket = socket(ai_family, ai_socktype, ai_protocol);
  if (_socket < 0) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to create socket (%s)", strerror(errno));
    return false;
  }

  if (!Anki::Messaging::SetReuseAddress(_socket, 1)) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to set reuseaddress (%s)", strerror(errno));
    close(_socket);
    _socket = -1;
    return false;
  }

  if (!Anki::Messaging::SetNonBlocking(_socket, 1)) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to set nonblocking (%s)", strerror(errno));
    close(_socket);
    _socket = -1;
    return false;
  }

  if (!Anki::Messaging::SetSendBufferSize(_socket, _sndbufsz)) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to set send buffer size %d (%s)", _sndbufsz, strerror(errno));
    close(_socket);
    _socket = -1;
    return false;
  }

  if (!Anki::Messaging::SetRecvBufferSize(_socket, _rcvbufsz)) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to set recv buffer size %d (%s)", _rcvbufsz, strerror(errno));
    close(_socket);
    _socket = -1;
    return false;
  }

  _sockname = sockname;
  _peername = peername;

  // Remove any existing socket using this name
  unlink(_sockname.c_str());

  // Bind to socket name
  memset(&_sockaddr, 0, sizeof(_sockaddr));
  _sockaddr.sun_family = ai_family;
  strncpy(_sockaddr.sun_path, _sockname.c_str(), sizeof(_sockaddr.sun_path));
  _sockaddr_len = (socklen_t) SUN_LEN(&_sockaddr);

  if (bind(_socket, (struct sockaddr *) &_sockaddr, _sockaddr_len) != 0) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to bind socket (%s)", strerror(errno));
    close(_socket);
    _socket = -1;
    return false;
  }

  // Connect to peer name
  memset(&_peeraddr, 0, sizeof(_peeraddr));
  _peeraddr.sun_family = ai_family;
  strncpy(_peeraddr.sun_path, _peername.c_str(), sizeof(_peeraddr.sun_path));
  _peeraddr_len = (socklen_t) SUN_LEN(&_peeraddr);

  if (connect(_socket, (struct sockaddr *) &_peeraddr, _peeraddr_len) != 0) {
    LOG_ERROR("LocalUdpClient.Connect", "Unable to connect to %s (%s)", peername.c_str(),  strerror(errno));
    close(_socket);
    _socket = -1;
    return false;
  }

  LOG_DEBUG("LocalUdpClient.Connect", "Connect from %s to %s on %d", sockname.c_str(), peername.c_str(), _socket);

  // Send connection packet (i.e. something so that the server adds us to the client list)
  Send(LocalUdpServer::kConnectionPacket, sizeof(LocalUdpServer::kConnectionPacket));

  return true;
}

bool LocalUdpClient::Disconnect()
{
  if (_socket > -1) {
    if (close(_socket) < 0) {
      LOG_ERROR("LocalUdpClient.Disconnect.Fail", 
                "Error closing socket %s (sock: %d) (%s)", 
                _sockname.c_str(), _socket, strerror(errno));
    };
    LOG_DEBUG("LocalUdpClient.Disconnect", "Disconnected %d", _socket);
    _socket = -1;
  }
  return true;
}

ssize_t LocalUdpClient::Send(const char* data, size_t size)
{
  if (_socket < 0) {
    LOG_ERROR("LocalUdpClient.Send", "Socket undefined, skipping send");
    return 0;
  }

  //LOG_DEBUG("LocalUdpClient.Send", "Sending %zu bytes", size);

  const ssize_t bytes_sent = send(_socket, data, size, 0);

  if (bytes_sent != size) {
    LOG_ERROR("LocalUdpClient.Send.Fail", 
              "Send error on %s (sock: %d), disconnecting (%s)", 
              _sockname.c_str(), _socket, strerror(errno));
    Disconnect();
    return -1;
  }

  return bytes_sent;
}

ssize_t LocalUdpClient::Recv(char* data, size_t maxSize)
{
  assert(data != NULL);

  if (_socket < 0) {
    LOG_ERROR("LocalUdpClient.Recv", "Socket undefined, skipping recv");
    return 0;
  }

  const ssize_t bytes_received = recv(_socket, data, maxSize, 0);

  if (bytes_received <= 0) {
    if (errno == EWOULDBLOCK) {
      //LOG_DEBUG("LocalUdpClient.Recv", "No data available");
      return 0;
    } else {
      LOG_ERROR("LocalUdpClient.Recv.Fail", 
                "Receive error on %s (sock: %d), dropping connection (%s)", 
                _sockname.c_str(), _socket, strerror(errno));
      Disconnect();
      return -1;
    }
  }

  //LOG_DEBUG("LocalUdpClient.Recv", "Received %zd bytes", bytes_received);
  return bytes_received;
}

ssize_t LocalUdpClient::GetIncomingSize() const
{
  if (_socket >= 0) {
    return Anki::Messaging::GetIncomingSize(_socket);
  }
  return -1;
}

ssize_t LocalUdpClient::GetOutgoingSize() const
{
  if (_socket >= 0) {
    return Anki::Messaging::GetOutgoingSize(_socket);
  }
  return -1;
}

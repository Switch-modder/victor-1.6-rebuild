#ifndef ANKI_MESSAGING_LOCAL_UDP_CLIENT_H
#define ANKI_MESSAGING_LOCAL_UDP_CLIENT_H

/**
 *
 * File: LocalUdpClient.h
 *
 * Description: Declaration of local-domain socket client class
 *
 * Copyright: Anki, inc. 2017
 *
 */
#include <string>

#include <sys/socket.h>
#include <sys/un.h>

class LocalUdpClient
{
public:

  LocalUdpClient(int sndbufsz, int rcvbufsz);
  LocalUdpClient();

  ~LocalUdpClient();

  bool Connect(const std::string& sockname, const std::string& peername);
  bool IsConnected() const { return _socket >= 0; }
  bool Disconnect();

  ssize_t Send(const char* data, size_t size);
  ssize_t Recv(char* data, size_t maxSize);

  // For use with select etc
  int GetSocket() const { return _socket; }

  // Return count of bytes queued for read or -1 on error
  ssize_t GetIncomingSize() const;

  // Return count of bytes queued for write or -1 on error
  ssize_t GetOutgoingSize() const;

private:
  // Socket parameters
  int _sndbufsz;
  int _rcvbufsz;

  // Socket descriptor
  int _socket;

  // Socket names
  std::string _sockname;
  std::string _peername;

  // Socket addresses
  struct sockaddr_un _sockaddr;
  socklen_t _sockaddr_len;

  struct sockaddr_un _peeraddr;
  socklen_t _peeraddr_len;

};

#endif

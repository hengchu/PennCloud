#ifndef INCLUDED_SOCKUTIL
#define INCLUDED_SOCKUTIL

#include <string>
#include <arpa/inet.h>

struct SockUtil {
  // This struct provides a namespace for utility functions that'll be
  // useful when dealing with sockets.

  static std::string sockAddrToString(const sockaddr_in& addr);
  // Convert the givne sockaddr_in to a string of the form "ip:port".
};

#endif

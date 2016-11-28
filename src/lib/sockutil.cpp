#include <sockutil.h>
#include <sstream>

std::string
SockUtil::sockAddrToString(const sockaddr_in& addr)
{
  std::stringstream ss;

  char buffer[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET,
	    &(addr.sin_addr),
	    buffer,
	    INET_ADDRSTRLEN);

  ss << std::string(buffer) << ':' << ntohs(addr.sin_port);

  return ss.str();
}

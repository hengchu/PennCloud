#include <sockutil.h>
#include <sstream>

std::string
SockUtil::sockAddrToString(const sockaddr_in& addr)
{
  std::stringstream ss;

  char buffer[18] = {0};
  inet_ntop(AF_INET, &addr, buffer, sizeof(addr));

  ss << std::string(buffer) << ':' << ntohs(addr.sin_port);

  return ss.str();
}

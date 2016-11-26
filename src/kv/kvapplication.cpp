#include <kvapplication.h>
#include <logutil.h>
#include <cstring>
#include <unistd.h>

namespace {
  const int k_BACKLOG_SIZE = 10;
}

KVApplication::KVApplication(const KVConfiguration& config,
			     int                    serverId)
  : d_running(false)
  , d_config(config)
  , d_serverId(serverId)
  , d_socket(-1)
  , d_serverAddr()
{
  int port = d_config.servers(serverId).port();

  LOG_INFO << "Creating KV application on port = "
	   << port
	   << LOG_END;
  
  std::memset(&d_serverAddr, 0, sizeof(d_serverAddr));
  d_serverAddr.sin_family      = AF_INET;
  d_serverAddr.sin_addr.s_addr = htons(INADDR_ANY);
  d_serverAddr.sin_port        = htons(port);
}

KVApplication::~KVApplication()
{
  // NOTHING
}

int
KVApplication::start()
{
  d_running = true;
  d_socket  = socket(AF_INET, SOCK_STREAM, 0);

  if (d_socket < 0) {
    LOG_ERROR << "Failed to create socket for server. "
	      << "Error = "
	      << std::strerror(errno)
	      << LOG_END;
    return -1;
  }

  int rc = bind(d_socket,
		reinterpret_cast<sockaddr *>(&d_serverAddr),
		sizeof(d_serverAddr));

  if (0 != rc) {
    LOG_ERROR << "Failed to bind server address. "
	      << "Error = "
	      << std::strerror(errno)
	      << LOG_END;
    return -1;
  }

  startListening();
  
  return 0;
}

void
KVApplication::startListening()
{
  int rc = listen(d_socket, k_BACKLOG_SIZE);

  if (0 != rc) {
    LOG_ERROR << "listen() failed! "
	      << "Error = "
	      << std::strerror(errno)
	      << ", giving up..."
	      << LOG_END;
    _exit(1);
  }

  while (d_running) {
    sockaddr_in connAddr;
    socklen_t   connAddrSize = sizeof(connAddr);

    int connSock = accept(d_socket,
			  reinterpret_cast<sockaddr *>(&connAddr),
			  &connAddrSize);

    if (connSock < 0) {
      LOG_ERROR << "Failed to accept() connection. "
		<< "Error = "
		<< std::strerror(errno)
		<< LOG_END;
      // Just ignore this connection and go to the next one.
      continue;
    }
  }
}

int
KVApplication::stop()
{
  return -1;
}

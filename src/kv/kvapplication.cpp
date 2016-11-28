#include <kvapplication.h>
#include <logutil.h>
#include <cstring>
#include <unistd.h>
#include <sockutil.h>
#include <kvserversession.h>
#include <arpa/inet.h>
#include <protoutil.h>
#include <kvprotocol.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <cassert>

namespace {
  const int k_BACKLOG_SIZE = 10;
}

KVApplication::KVApplication(const KVConfiguration& config,
			     int                    serverId)
  : d_running(false)
  , d_config(config)
  , d_serverId(serverId)
  , d_clientSocket(-1)
  , d_clientAddr()
  , d_cluster(config, serverId)
{
  int port = d_config.servers(serverId).client_addr().port();

  LOG_INFO << "Creating KV application on client listen port = "
	   << port
	   << LOG_END;

  std::memset(&d_clientAddr, 0, sizeof(d_clientAddr));
  d_clientAddr.sin_family = AF_INET;
  d_clientAddr.sin_port   = htons(port);
  int rc = inet_pton(AF_INET,
		     d_config.servers(serverId).client_addr().ip_address().c_str(),
		     &d_clientAddr.sin_addr.s_addr);

  if (1 != rc) {
    LOG_ERROR << "Failed to parse client listen ip address"
	      << LOG_END;
    _exit(1);
  }
}

KVApplication::~KVApplication()
{
  // NOTHING
}

void
KVApplication::listenForClients()
{
  d_clientSocket = socket(AF_INET, SOCK_STREAM, 0);

  if (d_clientSocket < 0) {
    LOG_ERROR << "Failed to create socket for clients. "
	      << " Error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }

  int rc = bind(d_clientSocket,
		reinterpret_cast<sockaddr *>(&d_clientAddr),
		sizeof(d_clientAddr));

  if (0 != rc) {
    LOG_ERROR << "Failed to bind client address. "
	      << "Error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }

  while (d_running) {
    rc = listen(d_clientSocket, k_BACKLOG_SIZE);

    if (0 != rc) {
      LOG_WARN << "listen() failed with rc = " << rc
	       << ", error = "
	       << std::strerror(errno)
	       << LOG_END;
      continue;
    }

    sockaddr_in clientAddr;
    socklen_t   clientAddrSize = sizeof(clientAddr);
    int clientSock = accept(d_clientSocket,
			    reinterpret_cast<sockaddr *>(&clientAddr),
			    &clientAddrSize);

    if (clientSock < 0) {
      LOG_WARN << "Failed to accept() client connection"
	       << ", error = "
	       << std::strerror(errno)
	       << LOG_END;
      continue;
    }

    LOG_INFO << "Established client connection from address = "
	     << SockUtil::sockAddrToString(clientAddr)
	     << LOG_END;
  }
}

int
KVApplication::start()
{
  d_running = true;

  // Start the TCP cluster.
  int rc = d_cluster.start();
  if (rc != 0) {
    LOG_ERROR << "Failed to start TCPCluster, rc = "
	      << rc
	      << LOG_END;
    return rc;
  }
  
  // Start listening for client connections.
  listenForClients();
  
  return 0;
}

int
KVApplication::stop()
{
  int rc = d_cluster.stop();
  if (rc != 0) {
    LOG_ERROR << "Failed to stop TCPCluster, rc = "
	      << rc
	      << LOG_END;
    return rc;
  }

  d_running = false;

  return 0;
}

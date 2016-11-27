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
  , d_socket(-1)
  , d_clientSocket(-1)
  , d_serverAddr()
  , d_clientAddr()
  , d_servers()
{
  int port = d_config.servers(serverId).listen_addr().port();

  LOG_INFO << "Creating KV application on server port = "
	   << port
	   << LOG_END;
  
  std::memset(&d_serverAddr, 0, sizeof(d_serverAddr));
  d_serverAddr.sin_family      = AF_INET;
  d_serverAddr.sin_addr.s_addr = htons(INADDR_ANY);
  d_serverAddr.sin_port        = htons(port);
  int rc = inet_pton(AF_INET,
		     d_config.servers(serverId).listen_addr().ip_address().c_str(),
		     &d_serverAddr.sin_addr.s_addr);

  if (1 != rc) {
    LOG_ERROR << "Failed to parse server listen ip address"
	      << LOG_END;

    _exit(1);
  }

  port = d_config.servers(serverId).client_addr().port();

  LOG_INFO << "Creating KV application on client port = "
	   << port
	   << LOG_END;

  std::memset(&d_clientAddr, 0, sizeof(d_clientAddr));
  d_clientAddr.sin_family = AF_INET;
  d_clientAddr.sin_port   = htons(port);
  rc = inet_pton(AF_INET,
		 d_config.servers(serverId).client_addr().ip_address().c_str(),
		 &d_serverAddr.sin_addr.s_addr);

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
KVApplication::makeConnectionWithServer(int serverId)
{
  sockaddr_in serverAddr;
  std::memset(&serverAddr, 0, sizeof(serverAddr));

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port   = htons(d_config.servers(serverId).listen_addr().port());

  int rc = inet_pton(AF_INET,
		     d_config.servers(serverId).listen_addr().ip_address().c_str(),
		     &serverAddr.sin_addr.s_addr);

  if (1 != rc) {
    LOG_ERROR << "Failed to parse IP address for server with id = "
	      << serverId
	      << ", giving up..."
	      << LOG_END;
    _exit(1);
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG_ERROR << "Failed to allocate socket."
	      << " Error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }
    
  rc = connect(sock,
	       reinterpret_cast<sockaddr *>(&serverAddr),
	       sizeof(serverAddr));
  if (rc < 0) {
    LOG_ERROR << "Failed to connect() to server id = "
	      << serverId
	      << ", error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }

  KVServerNegotiation negotiation;
  negotiation.set_from_server_id(d_serverId);
  assert(negotiation.IsInitialized());

  google::protobuf::io::FileOutputStream outputStream(sock);
  bool success = ProtoUtil::writeDelimitedTo(negotiation,
					     &outputStream);

  if (success) {
    LOG_INFO << "Successfully sent negotiation message to server = "
	     << serverId
	     << LOG_END;
  } else {
    LOG_ERROR << "Failed to write negotiation message to server = "
	      << serverId
	      << LOG_END;
    _exit(1);
  }

  d_servers[serverId] = new KVServerSession(sock);
}

void
KVApplication::makeConnections()
{
  for (int i = 0; i < d_serverId; ++i) {
    makeConnectionWithServer(i);

    LOG_INFO << "Established connection with server = "
	     << i
	     << LOG_END;
  }
}

void
KVApplication::listenForServers()
{
  for (int i = d_serverId + 1;
       i < d_config.servers_size();
       ++i) {
    LOG_INFO << "Waiting for server = "
	     << i
	     << "..."
	     << LOG_END;
    
    int rc = listen(d_socket, k_BACKLOG_SIZE);

    if (0 != rc) {
      LOG_ERROR << "listen() failed. "
		<< "Error = "
		<< std::strerror(errno)
		<< LOG_END;
      _exit(1);
    }

    sockaddr_in serverAddr;
    socklen_t   serverAddrSize = sizeof(serverAddr);
    int sock = accept(d_socket,
		      reinterpret_cast<sockaddr *>(&serverAddr),
		      &serverAddrSize);

    google::protobuf::io::FileInputStream inputStream(sock);
    
    KVServerNegotiation negotiationMessage;
    bool success = ProtoUtil::readDelimitedFrom(&inputStream,
						&negotiationMessage);

    if (success) {
      LOG_INFO << "Established connection with server = "
	       << negotiationMessage.from_server_id()
	       << ", at address = "
	       << SockUtil::sockAddrToString(serverAddr)
	       << LOG_END;
    } else {
      LOG_ERROR << "Failed to negotiate with connection from address = "
		<< SockUtil::sockAddrToString(serverAddr)
		<< LOG_END;
      _exit(1);
    }

    d_servers[negotiationMessage.from_server_id()] = new KVServerSession(sock);
  }
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

  makeConnections();

  // Only the server with the largest server id doesn't need to
  // listen.
  if (d_serverId < d_config.servers_size() - 1) {
    listenForServers();
  }

  // Start all the kv server sessions.
  for (auto it = d_servers.begin();
       it != d_servers.end();
       ++it) {
    int rc = it->second->start();

    if (rc != 0) {
      LOG_ERROR << "Failed to start server session object."
		<< " rc = "
		<< rc
		<< LOG_END;
      return rc;
    }
  }

  // Start listening for client connections.
  listenForClients();
  
  return 0;
}

int
KVApplication::stop()
{
  return -1;
}

#include <kvtcpcluster.h>
#include <cstring>
#include <logutil.h>
#include <sockutil.h>
#include <unistd.h>
#include <cassert>
#include <kvprotocol.pb.h>
#include <protoutil.h>
#include <functional>

namespace {

  const int k_BACKLOG_SIZE = 10;
  
  sockaddr_in
  sockaddrFromAddress(const Address& address)
  {    
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(address.port());

    int rc = inet_pton(AF_INET,
		       address.ip_address().c_str(),
		       &(addr.sin_addr));

    if (1 != rc) {
      LOG_ERROR << "Failed to parse IP address = "
		<< address.ip_address()
		<< LOG_END;
      _exit(1);
    }

    return addr;
  }
}

void
KVTCPCluster::enqueueRequest(int                    peerId,
			     const KVServerMessage& msg)
// Executed on KVServerSession thread.
{
  RequestContext req;
  req.d_peerId  = peerId;
  req.d_request = msg;
  
  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_outstandingRequestsLock);

    d_outstandingRequests.push(req);

    d_hasWork.notify_one();
  }
  // UNLOCK

  LOG_INFO << "Enqueued a request: "
	   << msg.DebugString()
	   << LOG_END;
}

KVTCPCluster::KVTCPCluster(const KVConfiguration&           config,
			   int                              serverId)
  : d_config(config)
  , d_serverId(serverId)
  , d_listenAddr()
  , d_socket(-1)
  , d_thread()
  , d_running(false)
  , d_serverSessions()
  , d_timer()
{
  d_listenAddr = sockaddrFromAddress(config.servers(d_serverId).listen_addr());
  
  LOG_INFO << "Created KVTCPCluster for server = "
	   << serverId
	   << ", listen on = "
	   << SockUtil::sockAddrToString(d_listenAddr)
	   << LOG_END;
}

KVTCPCluster::~KVTCPCluster()
{
  LOG_INFO << "Destroying KVTCPCluster..."
	   << LOG_END;

  assert(!d_running);
  close(d_socket);
}

void
KVTCPCluster::makeConnectionWithServer(int serverId)
{
  LOG_INFO << "Connecting to server = "
	   << serverId
	   << "..."
	   << LOG_END;
  
  sockaddr_in connAddr = sockaddrFromAddress(
			   d_config.servers(serverId).listen_addr());

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG_ERROR << "Failed to create socket."
	      << " error = "
	      << std::strerror(errno)
	      << LOG_END;
    // Just give up if we can't even create a socket.
    _exit(1);
  }

  int rc = connect(sock,
		   reinterpret_cast<sockaddr *>(&connAddr),
		   sizeof(connAddr));

  if (0 != rc) {
    LOG_ERROR << "Failed to connect() with server = "
	      << serverId
	      << ", rc = "
	      << rc
	      << ", error = "
	      << std::strerror(errno)
	      << LOG_END;
    close(sock);
    // Just give up on this one, will try again later.
    d_serverSessions[serverId] = nullptr;
    return;
  }

  KVServerNegotiation negotiation;
  negotiation.set_from_server_id(d_serverId);
  assert(negotiation.IsInitialized());

  google::protobuf::io::FileOutputStream outputStream(sock);
  bool success = ProtoUtil::writeDelimitedTo(negotiation,
					     &outputStream);

  if (!success) {
    LOG_ERROR << "Failed to write negotiation message to server = "
	      << serverId
	      << LOG_END;

    _exit(1);
  }

  d_serverSessions[serverId] =
    std::make_shared<KVServerSession>(sock,
				      serverId,
				      std::bind(&KVTCPCluster::enqueueRequest,
						this,
						std::placeholders::_1,
						std::placeholders::_2),
				      &d_timer);
}

void
KVTCPCluster::makeConnections()
{
  for (int i = 0; i < d_serverId; ++i) {
    makeConnectionWithServer(i);
  }
}

bool
KVTCPCluster::doneListening()
{
  int count = 0;
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    if (it->first > d_serverId) {
      ++count;
    }
  }

  // Check if all servers with greater ids have made connections.
  return (count == (d_config.servers_size() - d_serverId - 1));
}

void
KVTCPCluster::listenForServers()
{
  // Wait for all higher id'ed servers to connect.
  while (!doneListening()) {

    LOG_INFO << "Waiting for higher id servers ..."
	     << LOG_END;

    sockaddr_in connAddr;
    socklen_t   connAddrSize = sizeof(connAddr);
    
    int sock = accept(d_socket,
		      reinterpret_cast<sockaddr *>(&connAddr),
		      &connAddrSize);

    if (sock < 0) {
      LOG_ERROR << "accept() failed!"
		<< " error = "
		<< std::strerror(errno)
		<< LOG_END;
      continue;
    }

    LOG_INFO << "accepted a new connection from = "
	     << SockUtil::sockAddrToString(connAddr)
	     << LOG_END;

    google::protobuf::io::FileInputStream inputStream(sock);

    KVServerNegotiation negotiation;
    bool success = ProtoUtil::readDelimitedFrom(&inputStream,
						&negotiation);

    if (!success) {
      LOG_WARN << "Failed to read negotiation message from = "
	       << SockUtil::sockAddrToString(connAddr)
	       << ", closing this connection..."
	       << LOG_END;
      close(sock);
      continue;
    }

    LOG_INFO << "Acquired connection from server = "
	     << negotiation.from_server_id()
	     << ", from address = "
	     << SockUtil::sockAddrToString(connAddr)
	     << LOG_END;

    d_serverSessions[negotiation.from_server_id()] =
      std::make_shared<KVServerSession>(sock,
					negotiation.from_server_id(),
					std::bind(&KVTCPCluster::enqueueRequest,
						  this,
						  std::placeholders::_1,
						  std::placeholders::_2),
					&d_timer);
  }
}

void
KVTCPCluster::sendHeartBeats()
{
  LOG_INFO << "Sending heart beats to all peers"
	   << LOG_END;
  
  KVServerMessage heartbeat;
  heartbeat.mutable_heart_beat()->set_payload("hello, world!");
  
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    if (it->second) {
      int rc = it->second->sendRequest(heartbeat,
				       [](int peerId,
					  int status,
					  const KVServerMessage& req,
					  const KVServerMessage& resp){
					 LOG_INFO << status << LOG_END;
				       },
				       500);
      if (rc != 0) {
	LOG_WARN << "Failed to send heartbeat, rc = "
		 << rc
		 << LOG_END;
      }
    }
  }
}

void
KVTCPCluster::thread()
{
  // Make all the outgoing connections first.
  makeConnections();

  d_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (d_socket < 0) {
    LOG_ERROR << "Failed to create listen socket for server = "
	      << d_serverId
	      << ", error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }

  int rc = bind(d_socket,
		reinterpret_cast<sockaddr *>(&d_listenAddr),
		sizeof(d_listenAddr));

  if (0 != rc) {
    LOG_ERROR << "bind() failed!"
	      << " error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }
  
  rc = listen(d_socket, k_BACKLOG_SIZE);
  if (0 != rc) {
    LOG_ERROR << "listen() failed!"
	      << " error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }

  // Wait for all incoming connections.
  listenForServers();
  
  // Start all the server sessions.
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    int rc = 0;
    
    if (it->second) {
      it->second->start();
    }

    if (0 != rc) {
      LOG_ERROR << "Failed to start server session = "
		<< it->first
		<< LOG_END;
    }
  }

  LOG_INFO << "KVTCPCluster on server = "
	   << d_serverId
	   << " ready."
	   << LOG_END;

  while (d_running) {
    // LOCK
    {
      std::unique_lock<std::mutex> uniqueLock(d_outstandingRequestsLock);

      // Wait for up to 5 seconds
      // LOCK is released upon wait.
      d_hasWork.wait_for(uniqueLock, std::chrono::milliseconds(5000));
      // LOCK is acquired here.

      if (!d_outstandingRequests.empty()) {
	RequestContext request = d_outstandingRequests.front();
	d_outstandingRequests.pop();
	// UNLOCK before we process the request.
	uniqueLock.unlock();
	// PROCESS THE REQUEST
      } else {
	// If the queue is empty.
	uniqueLock.unlock();
	sendHeartBeats();
      }
    }
    // UNLOCK

    // Reap any potentially dead servers.
    reapDeadServers();
  }
}

std::set<int>
KVTCPCluster::reapDeadServers()
{
  // Check for disconnected sessions here.
  std::set<int> deadSessionIds;
  
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    if (!it->second || !it->second->alive()) {
      LOG_WARN << "Detected dead server session = "
	       << it->first
	       << ", will attempt to re-establish connection."
	       << LOG_END;
      deadSessionIds.insert(it->first);
      
      // Stop the sessions's thread loop.
      if (it->second) {
	it->second->stop();
      }
    }
  }

  for (auto it = deadSessionIds.begin();
       it != deadSessionIds.end();
       ++it) {      
    // Free the server session object.
    d_serverSessions.erase(*it);
  }

  return deadSessionIds;
}

int
KVTCPCluster::start()
{
  // Prevent double starting.
  if (d_running) {
    return -1;
  }
  
  d_running = true;

  LOG_INFO << "Spawning thread for KVTCPCluster..."
	   << LOG_END;
  
  d_thread = std::thread(&KVTCPCluster::thread, this);

  return 0;
}

int
KVTCPCluster::stop()
{
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    int rc = it->second->stop();
    if (rc != 0) {
      LOG_ERROR << "Failed to stop server session = "
		<< it->first
		<< LOG_END;
      return rc;
    }
  }

  d_running = false;
  
  // Unlock if we're waiting on something.
  d_hasWork.notify_one();
  d_thread.join();

  return 0;
}
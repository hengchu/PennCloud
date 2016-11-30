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
		     &d_clientAddr.sin_addr);

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

  rc = listen(d_clientSocket, k_BACKLOG_SIZE);
  
  if (0 != rc) {
    LOG_ERROR << "listen() failed with rc = " << rc
	      << ", error = "
	      << std::strerror(errno)
	      << LOG_END;
    _exit(1);
  }

  while (d_running) {  
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

    int clientId = d_clientId++;
    ClientSessionSP clientSession = std::make_shared<KVClientSession>(
					   clientId,
					   clientSock,
					   std::bind(&KVApplication::handleClientRequest,
						     this,
						     std::placeholders::_1,
						     std::placeholders::_2));

    // LOCK
    {
      std::lock_guard<std::mutex> guard(d_clientsLock);
      
      d_clients[clientId] = clientSession;
    }
    // UNLOCK

    clientSession->start();
    
    // Clean up any potential dead clients.
    reapDeadClients();
  }
}

void
KVApplication::reapDeadClients()
{
  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_clientsLock);

    std::set<int> deadClients;
    for (auto it = d_clients.begin(); it != d_clients.end(); ++it) {
      if (!it->second->alive()) {
	LOG_INFO << "Client = "
		 << it->second->clientId()
		 << " is dead. Marking it for removal."
		 << LOG_END;
	deadClients.insert(it->first);
      }
    }

    for (auto it = deadClients.begin(); it != deadClients.end(); ++it) {
      d_clients[*it]->stop();
      d_clients.erase(*it);
    }
  }
  // UNLOCK
}

void
KVApplication::handleClientRequest(int                     clientId,
				   const KVServiceRequest& request)
{
  KVServiceResponse resp;
  resp.set_request_id(request.request_id());
  
  switch (request.service_request_case()) {
  case KVServiceRequest::kPut: {
    // Do put.
    d_storage.put(request.put().column(),
		  request.put().row(),
		  request.put().value());

    resp.set_response_code(ResponseCode::SUCCESS);
  } break;
  case KVServiceRequest::kGet: {
    // Do get.
    std::string value;
    int rc = d_storage.get(&value,
			   request.get().column(),
			   request.get().row());

    if (0 == rc) {
      resp.set_response_code(ResponseCode::SUCCESS);
      resp.mutable_get()->set_value(value);
    } else {
      resp.set_response_code(ResponseCode::FAILURE);
      resp.mutable_failure()->set_error_message("No such row and column.");
    }
			   
  } break;
  case KVServiceRequest::kComparePut: {
    // Do CNP.
    int rc = d_storage.compareAndPut(request.compare_put().column(),
				     request.compare_put().row(),
				     request.compare_put().old_value(),
				     request.compare_put().new_value());
    if (0 == rc) {
      resp.set_response_code(ResponseCode::SUCCESS);
    } else if (-1 == rc) {
      resp.set_response_code(ResponseCode::FAILURE);
      resp.mutable_failure()->set_error_message("No such row and column.");
    } else if (-2 == rc) {
      resp.set_response_code(ResponseCode::SUCCESS);
      resp.mutable_failure()->set_error_message("Stored value doesn't match old value.");
    } else {
      // Should be unreachable.
      assert(false);
    }
  } break;
  case KVServiceRequest::kDelete: {
    // Do delete.
    int rc = d_storage.deleteValue(request.delete_().column(),
				   request.delete_().row());

    if (0 == rc) {
      resp.set_response_code(ResponseCode::SUCCESS);
    } else {
      resp.set_response_code(ResponseCode::FAILURE);
      resp.mutable_failure()->set_error_message("No such row and column.");
    }				   
  } break;
  default:
    // Invalid request.
    resp.set_response_code(ResponseCode::INVALID);
    resp.mutable_failure()->set_error_message("Invalid request type.");
  }
  
  ClientSessionSP clientSession;

  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_clientsLock);
    
    auto clientSessionIt = d_clients.find(clientId);

    if (clientSessionIt != d_clients.end() &&
	clientSessionIt->second->alive()) {
      clientSession = clientSessionIt->second;
    }
  }
  // UNLOCK

  int rc = clientSession->sendResponse(request.request_id(),
				       resp);

  if (0 != rc) {
    LOG_ERROR << "Failed to send response back to client = "
	      << clientId
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

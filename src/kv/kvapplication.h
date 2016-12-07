#ifndef INCLUDED_KVAPPLICATION
#define INCLUDED_KVAPPLICATION

#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <kvconfig.pb.h>
#include <map>
#include <memory>
#include <queue>

#ifndef INCLUDED_KVTCPCLUSTER
#include <kvtcpcluster.h>
#endif

#ifndef INCLUDED_KVSTORE
#include <kvstore.h>
#endif

#ifndef INCLUDED_KVCLIENTSESSION
#include <kvclientsession.h>
#endif

#ifndef INCLUDED_KVLOGMANAGER
#include <kvlogmanager.h>
#endif

class KVApplication : public KVLogManager {
  // This class defines the main mechanism that is the application
  // loop of kv storage service.

  // TYPES
  struct RequestContext {
    int             d_peerId;
    KVServerMessage d_request;
  };

  struct LogEntry {
    int              d_term;
    kvservice::KVServiceRequest d_request;
  };
  
  using ClientSessionSP = std::shared_ptr<KVClientSession>;

  std::atomic_bool                 d_running;
  // Whether this application is currently running.

  KVConfiguration                  d_config;
  // The configuration of the entire system.

  int                              d_clientId;
  // Next client id to assign.
  
  int                              d_serverId;
  // The id of this server.
  
  int                              d_clientSocket;
  // The socket that the application binds the client listening
  // address to.

  sockaddr_in                      d_clientAddr;
  // The address to bind to the client socket.
  
  KVTCPCluster                     d_cluster;
  // The cluster of servers.

  KVStore                          d_storage;
  // The key-value storage.

  std::map<int, ClientSessionSP>   d_clients;
  // A map from client id to client session pointers.

  std::mutex                       d_clientsLock;
  // The lock to protect the clients map.

  std::vector<LogEntry>            d_logs;
  // The requests this server has processed.

  std::mutex                       d_logsLock;
  // A lock to protect the log entries.
  
  void listenForClients();
  // Listen for incoming client connections.

  void handleClientRequest(int                                clientId,
			   const kvservice::KVServiceRequest& request);
  // Process the client request.

  void reapDeadClients();
  // Remove all the dead clients from the map.

  void sendResponseToClient(int                                 clientId,
			    int                                 requestId,
			    const kvservice::KVServiceResponse& resp);
  // Send the response to the given client.
  
 public:
  KVApplication(const KVConfiguration& config,
		int                    serverId);
  // Create an application on the given port number.

  ~KVApplication();
  // Destroy this application.
  
  int start();
  // Start the application. Return non-zero error code on
  // failure. Does NOT return if application is successfully started.
  // AKA the application runs on the calling thread.

  int stop();
  // Stop the application. Returns 0 on success, a non-zero error code
  // on failure.

  int numberOfLogEntries();
  // Return the number of log entries. Thread-safe.

  int append(int                                term,
	     const kvservice::KVServiceRequest& request);
  // Append a log entry, thread-safe.

  void retrieve(int                         *term,
		kvservice::KVServiceRequest *entry,
		int                          index);
  // Retrieve the entry at the given index. Thread-safe.

  void applyLog(int index);
  // Apply the log at the given index.

  void removeEntries(int index);
  // Remove all the entries at and after the given index.
};

#endif

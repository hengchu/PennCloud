#ifndef INCLUDED_KVTCPCLUSTER
#define INCLUDED_KVTCPCLUSTER

#ifndef INCLUDED_KVSERVERSESSION
#include <kvserversession.h>
#endif

#include <sys/socket.h>
#include <netinet/ip.h>
#include <kvconfig.pb.h>
#include <atomic>
#include <thread>
#include <map>
#include <memory>

class KVTCPCluster {
  // This class implements a mechanism that manages a set of
  // KVServerSession objects, and provides an interface for sending
  // request and handling responses from other servers in the cluster.
  
  // It will also try to automatically re-connect with disconnected
  // sessions.

  // Note that the cluster owns a thread for listening and
  // reconnecting server sessions, and each server session object also
  // owns a thread.

 private:
  using ServerSessionSP = std::shared_ptr<KVServerSession>;
  
  KVConfiguration                  d_config;
  // The configuration of the cluster.

  int                              d_serverId;
  // Server id of this node on the cluster.

  sockaddr_in                      d_listenAddr;
  // The address to listen on.
  
  int                              d_socket;
  // The socket to listen for other server connections.

  std::thread                      d_thread;
  // The thread id.

  std::atomic_bool                 d_running;
  // Whether the cluster is running or not.

  std::map<int, ServerSessionSP>   d_serverSessions;
  // A map from server id to server sessions. Note that the value in
  // the map can be a null pointer. In which case, it means that a
  // connection was attempted with that peer but failed.

  void makeConnectionWithServer(int serverId);
  // Block until connect()'ed with the given server.
  
  void makeConnections();
  // Block until all outgoing connect() calls are finished.

  bool doneListening();
  // Returns whether we're done listening or not.
  
  void listenForServers();
  // Listen until all servers with ids greater than mine are connected
  // to me.

  void sendHeartBeats();
  // Ping all peer servers.
  
  void thread();
  // The main thread loop.
  
 public:
  KVTCPCluster(const KVConfiguration& config,
	       int                    serverId);
  // Create a cluster.

  KVTCPCluster(const KVTCPCluster& other) = delete;
  KVTCPCluster& operator=(const KVTCPCluster& rhs) = delete;
  // NOT IMPLEMENTED.

  ~KVTCPCluster();
  // Destroy this cluster.

  int start();
  // Start the cluster. Return 0 on success, non-zero code on failure.
  // Note that this function returns immediately, it doesn't wait
  // until all server sessions are up.

  int stop();
  // Stop the cluster. Return 0 on success, non-zero error code on
  // failure.
  
};

#endif

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
#include <queue>

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

  struct RequestContext {
    int             d_peerId;
    KVServerMessage d_request;
  };
  
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

  std::queue<RequestContext>       d_outstandingRequests;
  // Outstanding requests from peer servers.

  std::mutex                       d_outstandingRequestsLock;
  // Lock to protect the queue of outstanding requests.

  std::condition_variable          d_hasWork;
  // A condition variable that signals whether we have work to process.

  // PRIVATE FUNCTIONS
  std::set<int> reapDeadServers();
  // Delete the server sessions that are dead, and return the set of
  // peer ids that were removed.
  
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

  void enqueueRequest(int                    peerId,
		      const KVServerMessage& msg);
  // Put the request into the queue of outstanding requests.
  // Executed on KVServerSession thread.
  
 public:
  KVTCPCluster(const KVConfiguration& config,
	       int                    serverId);
  // Create a cluster. The callback is passed onto the server
  // sessions, and is used as the request handler for each of the
  // server session created.

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

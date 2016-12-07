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
#include <condition_variable>
#include <timereventscheduler.h>
#include <kvlogmanager.h>

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
  using ServerSessionSP           = std::shared_ptr<KVServerSession>;
  using RequestStatus             = KVServerSession::RequestStatus;
  using ApplyNotificationCallBack = std::function<void()>;
  using ApplyNotificationMap      =
    std::map<int, std::vector<ApplyNotificationCallBack>>;

  struct RequestContext {
    int             d_peerId;
    KVServerMessage d_request;
  };
  
  KVConfiguration                  d_config;
  // The configuration of the cluster.

  int                              d_serverId;
  // Server id of this node on the cluster.

  KVLogManager                    *d_logManager_p;
  // Log manager, held, not owned.
  
  sockaddr_in                      d_listenAddr;
  // The address to listen on.
  
  int                              d_socket;
  // The socket to listen for other server connections.

  std::thread                      d_thread;
  // The thread id.

  std::atomic_bool                 d_running;
  // Whether the cluster is running or not.

  TimerEventScheduler              d_timer;

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

  ApplyNotificationMap             d_applyNotifications;
  // A map from log index to apply notifications callbacks.

  std::mutex                       d_applyNotificationsLock;
  // Lock to protect apply notifications.
  
  // -----------
  // RAFT STATES

  std::mutex                       d_raftLock;
  // Lock to protect the raft states.
  
  std::atomic_int                  d_votesReceived;
  // The number of votes this server has received this round.
  
  int                              d_electionTimeout;
  // Election timeout in number of milliseconds.

  std::atomic_int                  d_leaderId;
  // Current known leader's id. Could be -1 if no known leader exists.
  
  std::atomic_int                  d_currentTerm;
  // Current term number.

  std::atomic_int                  d_votedFor;
  // Candidate id that I have voted for this round, or -1 if I haven't
  // voted yet.

  std::atomic_int                  d_commitIndex;
  // Known highest index of committed logs.

  std::atomic_int                  d_appliedIndex;
  // Known highest index of applied logs.

  std::map<int, int>               d_nextIndices;
  // For each peer server, the index of the next log entry to send to
  // that server.

  std::map<int, int>               d_matchIndices;
  // For each peer server, the highest index of known replicated logs.

  // RAFT STATES
  // -----------
  
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

  void incrementTerm();
  // Increases current term by 1, also resets votes received this
  // round to 0.
  
  void convertToCandidate();
  // Vote for myself. Increment term. Send request vote.

  void convertToFollower(int term);
  // Convert myself to a follower while setting d_currentTerm = term.

  void convertToLeader();
  // Convert myself to leader and start sending out append entries
  // requests.

  void setRaftIndicesForPeer(int peerId,
			     int nextIndex,
			     int matchIndex);
  // Set the next and match indices for the given peer.

  bool shouldIncrementCommitIndex();
  // Check if we should increment the commit index.
  
  void processRequest(int                    peerId,
		      const KVServerMessage& request);
  // Process the request.

  void processAppendEntries(int                    peerId,
			    int                    contextId,
			    const KVAppendEntries& request);
  // Process an append entries request.
  
  void processRequestVote(int                  peerId,
			  int                  contextId,
			  const KVRequestVote& request);
  // Process a request vote request.

  void sendAppendEntries();

  void sendAppendEntriesToPeer(int peerId);
  // Send append entries request to the given peer.
  
  void logRaftStates();
  // Dump the raft states.

  void waitAndProcessRaftEventFollower(int waitTime);
  // One iteration of the raft event loop for a follower.

  void waitAndProcessRaftEventCandidate(int waitTime);
  // One iteration of the raft event loop for a candidate.

  void waitAndProcessRaftEventLeader(int waitTime);
  // One iteration of the raft event loop for a leader.
  
 public:
  KVTCPCluster(const KVConfiguration&  config,
	       int                     serverId,
	       KVLogManager           *logManager,
	       int                     term);
  // Create a cluster. The callback is passed onto the server
  // sessions, and is used as the request handler for each of the
  // server session created.
  // The term variable is the term loaded from persistent storage.

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

  bool isLeader();
  // Returns true if we're currently the leader.

  bool ready();
  // Returns true if the cluster is ready.
  
  void notifyWhenApplied(int                          index,
			 const std::function<void()>& cb);
  // Run the callback when log with the given index is commited.

  int currentTerm();
  // Return the current term.

  void leaderClientAddress(std::string *ipAddr,
			   int         *port);
  // Return the clinet address of the leader.
};

#endif

#include <kvtcpcluster.h>
#include <cstring>
#include <logutil.h>
#include <sockutil.h>
#include <unistd.h>
#include <cassert>
#include <kvprotocol.pb.h>
#include <protoutil.h>
#include <functional>
#include <random>

namespace {

  const int k_BACKLOG_SIZE = 10;
  const int k_RPC_TIMEOUT  = 2000;
  const int k_LEADER_WAIT  = 500;
  
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
}

KVTCPCluster::KVTCPCluster(const KVConfiguration&  config,
			   int                     serverId,
			   KVLogManager           *logManager)
  : d_config(config)
  , d_serverId(serverId)
  , d_logManager_p(logManager)
  , d_listenAddr()
  , d_socket(-1)
  , d_thread()
  , d_running(false)
  , d_timer()
  , d_serverSessions()
  , d_votesReceived(0)
  , d_electionTimeout(0)
  , d_leaderId(-1)
  , d_currentTerm(0)
  , d_votedFor(-1)
  , d_commitIndex(-1)
  , d_appliedIndex(-1)
  , d_nextIndices()
  , d_matchIndices()
{
  for (int i = 0; i < config.servers_size(); ++i) {
    if (i == serverId) {
      continue;
    }
    // TODO: next indices should be initialized to leader's last log
    // index + 1.
    d_nextIndices[i]  = 0;
    d_matchIndices[i] = -1;
  }
  
  d_listenAddr = sockaddrFromAddress(config.servers(d_serverId).listen_addr());
  
  LOG_INFO << "Created KVTCPCluster for server = "
	   << serverId
	   << ", listen on = "
	   << SockUtil::sockAddrToString(d_listenAddr)
	   << LOG_END;

  // Initialize the election timeout.
  std::random_device randomDevice;
  std::uniform_int_distribution<int> distribution(3000, 5000);
  d_electionTimeout = distribution(randomDevice);

  LOG_INFO << "Initialized election timeout = "
	   << d_electionTimeout
	   << " milliseconds on server = "
	   << d_serverId
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

  int enableReuse = 1;
  setsockopt(d_socket,
	     SOL_SOCKET,
	     SO_REUSEADDR,
	     &enableReuse,
	     sizeof(enableReuse));

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
      logRaftStates();
      
      // TODO: implement stuff in the section of RULES FOR ALL
      // SERVERS.
      if (d_commitIndex > d_appliedIndex) {
	d_appliedIndex += 1;
	d_logManager_p->applyLog(d_appliedIndex);

	{
	  // LOCK
	  std::lock_guard<std::mutex> guard(d_applyNotificationsLock);

	  for (int index = 0; index <= d_appliedIndex; ++index) {
	    auto it = d_applyNotifications.find(index);
	    if (it == d_applyNotifications.end()) {
	      continue;
	    }

	    auto notifications = it->second;
	    for (auto nit = notifications.begin();
		 nit != notifications.end();
		 ++nit) {
	      (*nit)();
	    }

	    d_applyNotifications.erase(it);
	  }	  
	  // UNLOCK
	}
      }
      
      if (d_leaderId != d_serverId &&
	  d_votedFor != d_serverId) {
	// We're a follower.
	LOG_INFO << "I am a follower."
		 << LOG_END;

	waitAndProcessRaftEventFollower(d_electionTimeout);
	
      } else if (d_leaderId != d_serverId &&
		 d_votedFor == d_serverId) {
	LOG_INFO << "I am a candidate."
		 << LOG_END;
	
	LOG_ERROR << "I have "
		  << d_votesReceived
		  << " votes"
		  << LOG_END;
	
	// Otherwise, we have not received enough votes yet. Continue
	// processing requests and potentially timeout.
        waitAndProcessRaftEventCandidate(d_electionTimeout);
      } else {
	// We're the leader.
	assert(d_serverId == d_leaderId);
	LOG_INFO << "I am a leader."
		 << LOG_END;

	// Check if we can increment commit index.
	if (shouldIncrementCommitIndex()) {
	  LOG_INFO << "Bumping the commit index on the leader."
		   << LOG_END;
	  d_commitIndex += 1;
	}
	
	waitAndProcessRaftEventLeader(k_LEADER_WAIT);
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

void
KVTCPCluster::incrementTerm()
{
  // LOCK
  std::lock_guard<std::mutex> guard(d_raftLock);
  d_currentTerm += 1;
  // UNLOCK
}

void
KVTCPCluster::convertToCandidate()
{
  // Increment term and reset votes received.
  incrementTerm();

  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_raftLock);
    // Vote for myself.
    d_votesReceived = 1;
    d_votedFor      = d_serverId;
    d_leaderId      = -1;
  }
  // UNLOCK
  
  // Send request votes.
  KVServerMessage msg;
  KVRequestVote& request = *(msg.mutable_request_vote());
  request.set_term(d_currentTerm);
  request.set_candidate_id(d_serverId);
  request.set_last_log_index(-1);
  request.set_last_log_term(-1);

  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    if (it->second) {
      ServerSessionSP session = it->second;
      session->sendRequest(msg,
			   /* Executed on kvserversession thread,
			      or the timer scheduler's thread
			   */			      
			   [=](int                    peerId,
			       RequestStatus          status,
			       const KVServerMessage& req,
			       const KVServerMessage& resp) {
			     // Bind the SP into the callback.
			     if (!session->alive()) {
			       LOG_WARN << "This session already died."
					<< LOG_END;
			       return;
			     }
			     
			     if (status == KVServerSession::TIMEDOUT) {
			       LOG_WARN << "Request for voting timed out."
					<< LOG_END;
			       return;
			     }

			     switch (resp.server_message_case()) {
			     case KVServerMessage::kResponse: {
			       // We could have incremented the term while waiting
			       // for the response.
			       if (resp.response().success() &&
				   resp.response().term() == d_currentTerm) {
				 LOG_ERROR << "Got a vote from peer = "
					   << peerId
					   << LOG_END;
				 d_votesReceived += 1;
				 d_hasWork.notify_one();
			       } else if (resp.response().term() > d_currentTerm) {
				 convertToFollower(resp.response().term());
			       }
			     } break;
			     default: {
			       LOG_ERROR << "Unexpected response type = "
					 << resp.server_message_case()
					 << LOG_END;
			     } break;
			     }
			   },
			   k_RPC_TIMEOUT);
    }
  }
}

void
KVTCPCluster::processRequest(int                    peerId,
			     const KVServerMessage& request)
{
  switch(request.server_message_case()) {
  case KVServerMessage::kAppendEntries: {
    processAppendEntries(peerId,
			 request.context_id(),
			 request.append_entries());
  } break;
  case KVServerMessage::kRequestVote: {
    processRequestVote(peerId,
		       request.context_id(),
		       request.request_vote());
  } break;
  default: {
    LOG_ERROR << "Received unexpected request type = "
	      << request.server_message_case()
	      << " from peer = "
	      << peerId
	      << LOG_END;
  } break;    
  }
}

void
KVTCPCluster::processRequestVote(int                  peerId,
				 int                  contextId,
				 const KVRequestVote& request)
{
  KVServerMessage msg;
  KVResponse& voteResp = *(msg.mutable_response());

  int candidateLastLogIndex = request.last_log_index();
  int candidateLastLogTerm  = request.last_log_term();

  int lastLogIndex = d_logManager_p->numberOfLogEntries() - 1;
  int lastLogTerm = -1;
  KVServiceRequest lastLogEntry;

  if (lastLogIndex >= 0) {
    d_logManager_p->retrieve(&lastLogTerm,
			     &lastLogEntry,
			     lastLogIndex);
  }
  
  if (request.term() < d_currentTerm) {
    voteResp.set_success(false);
    voteResp.set_term(d_currentTerm);
  } else if (request.term() == d_currentTerm) {
    // We vote here.

    if (d_votedFor == -1 &&
	lastLogIndex <= candidateLastLogIndex &&
	lastLogTerm  == candidateLastLogTerm) {
      LOG_INFO << "Voting for peer = "
	       << peerId
	       << " in term = "
	       << d_currentTerm
	       << LOG_END;
      voteResp.set_success(true);
      voteResp.set_term(d_currentTerm);
    }
  } else if (request.term() > d_currentTerm) {
    convertToFollower(request.term());
    assert(d_votedFor == -1);

    if (d_votedFor == -1 &&
	lastLogIndex <= candidateLastLogIndex &&
	lastLogTerm  <= candidateLastLogTerm) {
      LOG_INFO << "Voting for peer = "
	       << peerId
	       << " in term = "
	       << d_currentTerm
	       << LOG_END;
      voteResp.set_success(true);
      voteResp.set_term(d_currentTerm);
    }
  } else {
    // Unreachable code.
    assert(false);
  }

  d_serverSessions[peerId]->sendResponse(contextId,
					 msg);
}

void
KVTCPCluster::processAppendEntries(int                    peerId,
				   int                    contextId,
				   const KVAppendEntries& request)
{
  KVServerMessage msg;
  KVResponse& resp = *(msg.mutable_response());
  resp.set_term(d_currentTerm);

  LOG_INFO << "Got appendEntries from peer = "
	   << peerId
	   << ", msg = "
	   << request.DebugString()
	   << LOG_END;
  
  if (request.term() < d_currentTerm) {
    resp.set_success(false);
  } else {
    d_currentTerm = request.term();
    d_leaderId    = request.leader_id();
    
    int myLogTermAtRequestIndex = -1;
    KVServiceRequest myLogEntryAtRequestIndex;
    
    if (request.prev_log_index()
	< d_logManager_p->numberOfLogEntries()) {
      // There is a previous log entry.
      if (request.prev_log_index() >= 0) {
	d_logManager_p->retrieve(&myLogTermAtRequestIndex,
				 &myLogEntryAtRequestIndex,
				 request.prev_log_index());

	// The term matched.
	if (myLogTermAtRequestIndex == request.prev_log_term()) {
	  resp.set_success(true);
	  // TODO: Actually do the appending here.
	  int nextIndex = request.prev_log_index() + 1;
	  if (nextIndex < d_logManager_p->numberOfLogEntries()) {
	    d_logManager_p->removeEntries(nextIndex);
	  }
	  
	  for (auto it = request.entries().begin();
	       it != request.entries().end();
	       ++it) {
	    const KVServiceRequest& entryToAppend = *it;
	    d_logManager_p->append(request.term(),
				   entryToAppend);
	  }
	  
	  // Update the commit index if the leader committed more than
	  // we did.
	  if (request.leader_commit() > d_commitIndex) {
	    d_commitIndex = std::min(request.leader_commit(),
				     d_logManager_p->numberOfLogEntries()-1);
	  }
	} else {
	  LOG_ERROR << "AppendEntries() failed because"
		    << " log term didn't match."
		    << " my term = " << myLogTermAtRequestIndex
		    << ", request term = " << request.prev_log_term()
		    << LOG_END;
	  // The term didn't match.
	  resp.set_success(false);
	}
      } else {
	// There was no previous log entry.
	resp.set_success(true);
	assert(request.prev_log_index() == -1);
	
	d_logManager_p->removeEntries(0);
	for (auto it = request.entries().begin();
	     it != request.entries().end();
	     ++it) {
	  const KVServiceRequest& entryToAppend = *it;
	  d_logManager_p->append(request.term(),
				 entryToAppend);
	}

	if (request.leader_commit() > d_commitIndex) {
	  d_commitIndex = std::min(request.leader_commit(),
				   d_logManager_p->numberOfLogEntries()-1);
	}
      }
    } else {
      LOG_ERROR << "AppendEntries() failed because "
		<< "prev log index is larger than my log storage size."
		<< LOG_END;
      resp.set_success(false);
    }
  }

  d_serverSessions[peerId]->sendResponse(contextId,
					 msg);
}

void
KVTCPCluster::convertToFollower(int term)
{
  // LOCK
  std::lock_guard<std::mutex> guard(d_raftLock);
  
  assert(term > d_currentTerm);
  
  d_currentTerm   = term;
  d_votedFor      = -1;
  d_votesReceived = 0;
  // UNLOCK
}

void
KVTCPCluster::logRaftStates()
{ 
  // LOCK
  std::lock_guard<std::mutex> guard(d_raftLock);
 
  LOG_ERROR << "[ votesReceived = " << d_votesReceived
	    << ", electionTimeout = " << d_electionTimeout
	    << ", leaderId = " << d_leaderId
	    << ", currentTerm = " << d_currentTerm
	    << ", votedFor = " << d_votedFor
	    << " ]"
	    << LOG_END;

  for (int i = 0;
       i < d_logManager_p->numberOfLogEntries();
       ++i) {
    int term;
    KVServiceRequest request;
    d_logManager_p->retrieve(&term,
			     &request,
			     i);

    LOG_ERROR << "Log[" << i << "]"
	      << " = "
	      << request.DebugString()
	      << LOG_END;
  }

  // Log the next and match indices
  if (d_leaderId == d_serverId) {
    std::stringstream ss;

    ss << "[";
    for (int i = 0; i < d_config.servers_size(); ++i) {
      if (i == d_leaderId) {
	continue;
      }

      ss << ", [ "
	 << i
	 << " = ("
	 << d_matchIndices[i]
	 << ", "
	 << d_nextIndices[i]
	 << ")]";
    }

    ss << "]";

    auto state = ss.str();
    
    LOG_ERROR << state << LOG_END;
  }
  // UNLOCK
}

void
KVTCPCluster::convertToLeader()
{
  // LOCK
  std::lock_guard<std::mutex> guard(d_raftLock);

  d_leaderId = d_serverId;
  
  // Reset next and match indicies.
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    d_matchIndices[it->first] = -1;
    d_nextIndices[it->first]  = d_logManager_p->numberOfLogEntries();
  }
  
  // UNLOCK
}

void
KVTCPCluster::waitAndProcessRaftEventFollower(int waitTime)
{
  std::unique_lock<std::mutex> uniqueLock(d_outstandingRequestsLock);

  // Wait for up to election timeout amount of milliseconds.
  // LOCK is released upon wait.
  auto releaseTime = std::chrono::steady_clock::now()
    + std::chrono::milliseconds(waitTime);
  
  std::cv_status status = std::cv_status::no_timeout;
  // To avoid spurious wakeup.
  // Break out of the wait loop if we got a request, or we timed
  // out.
  while (d_outstandingRequests.empty() &&
	 status != std::cv_status::timeout) {
    status = d_hasWork.wait_until(uniqueLock,
				  releaseTime);
  }
  // LOCK is acquired here.
  
  // Election time out triggered.
  if (status == std::cv_status::timeout) {
    assert(d_outstandingRequests.empty());
    // TODO: We should convert to candidate and start an
    // election here.
    convertToCandidate();
  } else {
    if (!d_outstandingRequests.empty()) {
      RequestContext request = d_outstandingRequests.front();
      d_outstandingRequests.pop();
      // PROCESS THE REQUEST
      processRequest(request.d_peerId,
		     request.d_request);
    }
  }
}

void
KVTCPCluster::waitAndProcessRaftEventCandidate(int waitTime)
{
  std::unique_lock<std::mutex> uniqueLock(d_outstandingRequestsLock);

  // Wait for up to election timeout amount of milliseconds.
  // LOCK is released upon wait.
  auto releaseTime = std::chrono::steady_clock::now()
    + std::chrono::milliseconds(waitTime);
  
  std::cv_status status = std::cv_status::no_timeout;
  // To avoid spurious wakeup.
  // Break out of the wait loop if we got a request, or we timed
  // out.
  while (d_outstandingRequests.empty() &&
	 status != std::cv_status::timeout &&
	 d_votesReceived <= d_config.servers_size() / 2) {
    status = d_hasWork.wait_until(uniqueLock,
				  releaseTime);
  }
  // LOCK is acquired here.

  // We're a candidate.
  if (d_votesReceived > d_config.servers_size() / 2) {
    // TODO: We've received majority amount of votes. Convert to
    // leader, start sending append entries.
    LOG_INFO << "I am the new LEADER of term = "
	     << d_currentTerm
	     << LOG_END;
    
    convertToLeader();
    return;
  }
  
  // Election time out triggered.
  if (status == std::cv_status::timeout) {
    assert(d_outstandingRequests.empty());
    // TODO: We should convert to candidate and start an
    // election here.
    convertToCandidate();
  } else {
    if (!d_outstandingRequests.empty()) {
      RequestContext request = d_outstandingRequests.front();
      d_outstandingRequests.pop();
      // PROCESS THE REQUEST
      processRequest(request.d_peerId,
		     request.d_request);
    }
  }
}

void
KVTCPCluster::waitAndProcessRaftEventLeader(int waitTime)
{
  std::unique_lock<std::mutex> uniqueLock(d_outstandingRequestsLock);

  // Wait for up to election timeout amount of milliseconds.
  // LOCK is released upon wait.
  auto releaseTime = std::chrono::steady_clock::now()
    + std::chrono::milliseconds(waitTime);
  
  std::cv_status status = std::cv_status::no_timeout;
  // To avoid spurious wakeup.
  // Break out of the wait loop if we got a request, or we timed
  // out.
  while (d_outstandingRequests.empty() &&
	 status != std::cv_status::timeout) {
    status = d_hasWork.wait_until(uniqueLock,
				  releaseTime);
  }
  // LOCK is acquired here.
  
  // Election time out triggered.
  if (status == std::cv_status::timeout) {
    assert(d_outstandingRequests.empty());
    sendAppendEntries();
  } else {
    if (!d_outstandingRequests.empty()) {
      RequestContext request = d_outstandingRequests.front();
      d_outstandingRequests.pop();
      // PROCESS THE REQUEST
      processRequest(request.d_peerId,
		     request.d_request);
    }
  }
}

bool
KVTCPCluster::isLeader()
{
  return d_leaderId == d_serverId;
}

void
KVTCPCluster::sendAppendEntries()
{
  for (auto it = d_serverSessions.begin();
       it != d_serverSessions.end();
       ++it) {
    if (it->second) {
      sendAppendEntriesToPeer(it->first);
    }
  }
}

void
KVTCPCluster::sendAppendEntriesToPeer(int peerId)
{
  KVServerMessage msg;
  KVAppendEntries& request = *(msg.mutable_append_entries());

  int peerNextIndex = -1;

  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_raftLock);
    peerNextIndex = d_nextIndices[peerId];
  }
  // UNLOCK
  
  request.set_term(d_currentTerm);
  request.set_leader_id(d_serverId);
  request.set_leader_commit(d_commitIndex);
  request.set_prev_log_index(peerNextIndex - 1);
  request.set_prev_log_term(-1);

  assert(request.leader_commit() == d_commitIndex);

  LOG_ERROR << "Peer next index = "
	    << peerNextIndex
	    << LOG_END;
  LOG_ERROR << request.DebugString() << LOG_END;

  // Check if we have this entry or not.
  if (peerNextIndex < d_logManager_p->numberOfLogEntries()) {
    // Send append entry with this entry.
    int              entryTerm;
    KVServiceRequest entry;
    d_logManager_p->retrieve(&entryTerm,
			     &entry,
			     peerNextIndex);
    *(request.mutable_entries())->Add() = entry;
    assert(request.mutable_entries()->size() == 1);

    if (peerNextIndex - 1 >= 0) {
      int              prevTerm;
      KVServiceRequest prevEntry;
      d_logManager_p->retrieve(&prevTerm,
			       &prevEntry,
			       peerNextIndex - 1);
      request.set_prev_log_term(prevTerm);

      auto msgDump = msg.DebugString();
      LOG_INFO << "Sending appendEntries() = "
	       << msgDump
	       << ", to peer = "
	       << peerId
	       << LOG_END;
      assert(request.prev_log_term() != -1);
    }
  }

  
  ServerSessionSP session = d_serverSessions[peerId];

  session->sendRequest(msg,
		       /* Executed on the server session thread
			  or the timer thread in case of a
			  timeout.
		       */
		       // Note that we capture by
		       // value the variable
		       // peerNextIndex and request
		       // in the body of this lambda.
		       [=](int                    peerId,
			   RequestStatus          status,
			   const KVServerMessage& req,
			   const KVServerMessage& resp){
			 if (!session->alive()) {
			   LOG_WARN << "This session already died."
				    << LOG_END;
			   return;
			 }
			 
			 if (status == KVServerSession::TIMEDOUT) {
			   LOG_WARN << "Append entries to peer = "
				    << peerId
				    << " timed out."
				    << LOG_END;
			   return;
			 }

			 LOG_ERROR << "Got appendEntries() response = "
				   << resp.DebugString()
				   << " from peer = "
				   << peerId
				   << LOG_END;
			 
			 switch(resp.server_message_case()) {
			 case KVServerMessage::kResponse: {
			   const KVResponse& response = resp.response();
			   
			   if (response.term() > d_currentTerm) {
			     LOG_WARN << "Demoting myself "
				      << "to follower because "
				      << "I received a response "
				      << "with higher term."
				      << LOG_END;
			     assert(!response.success());
			     convertToFollower(response.term());
			     return;
			   }
			   
			   if (response.success()) {
			     setRaftIndicesForPeer(
				   peerId,
				   peerNextIndex
				        + request.entries().size(),
			           peerNextIndex
				        + request.entries().size() - 1);
			   } else {
			     // Will retry the next
			     // time leader loops
			     // around.
			     {
			       // LOCK
			       std::lock_guard<std::mutex> guard(d_raftLock);
			       d_nextIndices[peerId] -= 1;

			       LOG_ERROR << "Decrementing next index for peer = "
					 << peerId
					 << LOG_END;
			     }
			     // UNLOCK
			   }
			 } break;
			 default: {
			   LOG_ERROR << "Unexpected message type = "
				     << resp.server_message_case()
				     << LOG_END;
			 } break;
			 }
		       },
		       k_RPC_TIMEOUT);
}

void
KVTCPCluster::setRaftIndicesForPeer(int peerId,
				    int nextIndex,
				    int matchIndex)
{
  LOG_INFO << "Setting peer["
	   << peerId
	   << "]"
	   << " next = "
	   << nextIndex
	   << ", match = "
	   << nextIndex - 1
	   << LOG_END;
  
  // LOCK
  std::lock_guard<std::mutex> guard(d_raftLock);

  d_nextIndices[peerId]  = nextIndex;
  d_matchIndices[peerId] = matchIndex;
  
  // UNLOCK
}

bool
KVTCPCluster::shouldIncrementCommitIndex()
{
  // LOCK
  std::lock_guard<std::mutex> guard(d_raftLock);
  
  int matchSize = 0;
  
  for (auto it = d_matchIndices.begin();
       it != d_matchIndices.end();
       ++it) {    
    if (it->second > d_commitIndex) {
      LOG_INFO << "Peer["
	       << it->first
	       << "] has match = "
	       << it->second
	       << ", while commitIndex = "
	       << d_commitIndex
	       << LOG_END;
      matchSize += 1;
    }
  }

  return matchSize > (d_config.servers_size() / 2);
  // UNLOCK
}

void
KVTCPCluster::notifyWhenApplied(int                          index,
				const std::function<void()>& cb)
{
  // LOCK
  std::lock_guard<std::mutex> guard(d_applyNotificationsLock);

  auto it = d_applyNotifications.find(index);

  if (it == d_applyNotifications.end()) {
    d_applyNotifications[index] = std::vector<std::function<void()>>();
    it = d_applyNotifications.find(index);
  }

  assert(it != d_applyNotifications.end());

  it->second.push_back(cb);
  // UNLOCK
}

int
KVTCPCluster::currentTerm()
{
  return d_currentTerm;
}

void
KVTCPCluster::leaderClientAddress(std::string *ip,
				  int         *port)
{
  int leader = d_leaderId;

  *ip = d_config.servers(leader).client_addr().ip_address();
  *port = d_config.servers(leader).client_addr().port();
}

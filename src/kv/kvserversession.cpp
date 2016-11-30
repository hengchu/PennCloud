#include <kvserversession.h>
#include <logutil.h>
#include <sys/socket.h>
#include <unistd.h>
#include <kvprotocol.pb.h>
#include <protoutil.h>
#include <cstring>

namespace {
  int setRequestBit(int contextId)
  {
    return (contextId | 1 << 31);
  }

  int clearRequestBit(int contextId)
  {
    return (contextId & ~(1 << 31));
  }

  bool isRequestBitSet(int contextId)
  {
    return contextId == setRequestBit(contextId);
  }

}

KVServerSession::KVServerSession(int                  socket,
				 int                  peerId,
				 const CallBack&      callback,
				 TimerEventScheduler *timer)
  : d_socket(socket)
  , d_peerId(peerId)
  , d_thread()
  , d_running(false)
  , d_alive(true)
  , d_contextId(0)
  , d_inputStream(socket)
  , d_outputStream(socket)
  , d_outstandingRequests()
  , d_outstandingRequestsLock()
  , d_serverRequestHandler(callback)
  , d_timerEventScheduler_p(timer)
{
  // NOTHING
}

KVServerSession::~KVServerSession()
{
  close(d_socket);
}

void
KVServerSession::requestTimedOut(int contextId)
// Runs on the timer scheduler thread.
{
  LOG_INFO << "Request = "
	   << contextId
	   << " timed out, removing it from the context map."
	   << LOG_END;

  RequestCallBack cb;
  KVServerMessage req;
  
  {
    std::lock_guard<std::mutex> guard(d_outstandingRequestsLock);

    auto it = d_outstandingRequests.find(contextId);
    if (it != d_outstandingRequests.end()) {
      cb  = it->second.d_callback;
      req = it->second.d_request;
      d_outstandingRequests.erase(it);
    }
  }

  // Invoke the callback with a timed out status.
  if (cb) {
    cb(d_peerId, TIMEDOUT, req, KVServerMessage());
  }
}

void
KVServerSession::threadLoop()
{
  while (d_running && d_alive) {
    KVServerMessage serverMessage;

    LOG_INFO << "Waiting for a server message..."
	     << LOG_END;
    
    bool success = ProtoUtil::readDelimitedFrom(&d_inputStream,
						&serverMessage);
      
    if (!success) {
      LOG_WARN << "Server session with peer = "
	       << d_peerId
	       << " is broken."
	       << LOG_END;
	
      d_alive = false;
      break;
    }

    LOG_DEBUG << "Got a message from server = "
	      << d_peerId
	      << ", request = "
	      << serverMessage.DebugString()
	      << LOG_END;
    
    int contextId = serverMessage.context_id();

    if (isRequestBitSet(contextId)) {
      LOG_INFO << "Got a request from server = "
	       << d_peerId
	       << LOG_END;
      d_serverRequestHandler(d_peerId, serverMessage);
    } else {
      LOG_INFO << "Got a response from server = "
	       << d_peerId
	       << LOG_END;

      // Find the callback in the outstanding map.
      RequestCallBack cb;
      KVServerMessage req;
      
      // LOCK
      {
	std::lock_guard<std::mutex> guard(d_outstandingRequestsLock);
	auto it = d_outstandingRequests.find(contextId);
	if (it == d_outstandingRequests.end()) {
	  LOG_WARN << "Got a response with contextId = "
		   << contextId
		   << ", from peer = "
		   << d_peerId
		   << ", but there is no corresponding outstanding request!"
		   << LOG_END;
	} else {
	  cb  = it->second.d_callback;
	  req = it->second.d_request;
	  d_outstandingRequests.erase(it);
	}
      }
      // UNLOCK

      if (cb) {
	cb(d_peerId, SUCCESS, req, serverMessage);
      }
    }
  }
}

int
KVServerSession::start()
{
  if (d_running) {
    return -1;
  }
  
  d_running = true;
  d_thread = std::thread(&KVServerSession::threadLoop, this);
  return 0;
}

int
KVServerSession::stop()
{
  // Avoid double stopping.
  if (!d_running) {
    return -1;
  }
  
  d_running = false;

  if (alive()) {
    int rc = shutdown(d_socket, SHUT_RDWR);
    
    if (0 != rc) {
      LOG_ERROR << "Failed to shutdown server session socket"
		<< ", error = "
		<< std::strerror(errno)
		<< LOG_END;
    }
  }

  if (d_thread.joinable()) {
    d_thread.join();
  }
  return 0;
}

bool
KVServerSession::alive()
{
  return d_alive;
}

int
KVServerSession::sendRequest(const KVServerMessage& message,
			     const RequestCallBack& cb,
			     uint64_t               timeoutMilliseconds)
{
  if (!alive()) {
    return -1;
  }
  
  assert(message.IsInitialized());
  
  KVServerMessage msgToSend = message;
  d_contextId += 1;
  msgToSend.set_context_id(setRequestBit(d_contextId));
  
  // If an empty callback is specified (meaning we're not expecting a
  // response), we don't need to bother inserting it into the
  // outstanding request map.
  if (cb) {
    // LOCK
    {
      std::lock_guard<std::mutex> guard(d_outstandingRequestsLock);

      RequestContext ctx;
      ctx.d_callback   = cb;
      ctx.d_request    = msgToSend;
      ctx.d_hasTimeout = false;

      if (timeoutMilliseconds > 0) {
	ctx.d_hasTimeout = true;
	ctx.d_timerHandle = d_timerEventScheduler_p->schedule(
				timeoutMilliseconds,
				0,
				std::bind(&KVServerSession::requestTimedOut,
					  this,
					  int(d_contextId)));
      }
      
      d_outstandingRequests[d_contextId] = ctx;
    }
    // UNLOCK
  }
  
  bool success = ProtoUtil::writeDelimitedTo(msgToSend,
					     &d_outputStream);
  
  if (!success) {
    LOG_WARN << "KVServerSession::sendRequest() failed."
	     << " Marking session = "
	     << d_peerId
	     << " as dead."
	     << LOG_END;
    d_alive = false;
    return -1;
  }

  d_outputStream.Flush();

  return 0;
}

int
KVServerSession::sendResponse(int                    contextId,
			      const KVServerMessage& message)
{
  if (!alive()) {
    return -1;
  }
  
  assert(message.IsInitialized());

  KVServerMessage msgToSend = message;
  msgToSend.set_context_id(clearRequestBit(contextId));

  bool success = ProtoUtil::writeDelimitedTo(msgToSend,
					     &d_outputStream);

  if (!success) {
    LOG_WARN << "KVServerSession::sendResponse() failed."
	     << " Marking session = "
	     << d_peerId
	     << " as dead."
	     << LOG_END;
    d_alive = false;
    return -1;
  }

  d_outputStream.Flush();

  return 0;
}

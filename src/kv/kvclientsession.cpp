#include <kvclientsession.h>
#include <logutil.h>
#include <protoutil.h>
#include <unistd.h>

using namespace kvservice;

KVClientSession::KVClientSession(int             clientId,
				 int             socket,
				 const CallBack& requestHandler)
  : d_clientId(clientId)
  , d_socket(socket)
  , d_alive(true)
  , d_running(false)
  , d_thread()
  , d_requestHandler(requestHandler)
  , d_inputStream(socket)
  , d_outputStream(socket)
{
  LOG_INFO << "Created client session = "
	   << clientId
	   << LOG_END;
}

KVClientSession::~KVClientSession()
{
  LOG_INFO << "Destroying client session = "
	   << d_clientId
	   << LOG_END;

  close(d_socket);
}

int
KVClientSession::start()
{
  if (d_running) {
    return -1;
  }
  
  d_running = true;
  d_thread  = std::thread(&KVClientSession::threadLoop, this);

  return 0;
}

int
KVClientSession::stop()
{
  if (!d_running) {
    return -1;
  }
  
  d_running = false;

  if (d_thread.joinable()) {
    d_thread.join();
  }

  return 0;
}

int
KVClientSession::sendResponse(int                      requestId,
			      const KVServiceResponse& response)
{
  if (!alive()) {
    return -1;
  }
  
  KVServiceResponse respToSend = response;
  respToSend.set_request_id(requestId);

  bool success = ProtoUtil::writeDelimitedTo(response,
					     &d_outputStream);

  if (!success) {
    LOG_WARN << "Failed to send() on client session = "
	     << d_clientId
	     << LOG_END;
    d_alive = false;
    return -1;
  }

  d_outputStream.Flush();
  
  return 0;
}

void
KVClientSession::threadLoop()
{
  while (d_running && d_alive) {
    KVServiceRequest request;
    
    bool success = ProtoUtil::readDelimitedFrom(&d_inputStream,
						&request);

    if (!success) {
      LOG_WARN << "Failed to read() in client session = "
	       << d_clientId
	       << LOG_END;
      d_alive = false;
      break;
    }

    LOG_DEBUG << "Got a request from client = "
	      << d_clientId
	      << " request = "
	      << ProtoUtil::truncatedDebugString(request)
	      << LOG_END;
    
    d_requestHandler(d_clientId, request);
  }
}

bool
KVClientSession::alive()
{
  return d_alive;
}

int
KVClientSession::clientId()
{
  return d_clientId;
}

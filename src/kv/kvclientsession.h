#ifndef INCLUDED_KVCLIENTSESSION
#define INCLUDED_KVCLIENTSESSION

#include <kvservicemessages.pb.h>
#include <functional>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <thread>
#include <atomic>

class KVClientSession {
  // This class implements a mechanism that is the session between a
  // KV server and a client.
 public:
  using CallBack = std::function<void(int                     clientId,
				      const KVServiceRequest& request)>;
  // A request handler callback.
  
 private:

  using FileInputStream  = google::protobuf::io::FileInputStream;
  using FileOutputStream = google::protobuf::io::FileOutputStream;

  int               d_clientId;
  // The client id.
  
  int               d_socket;
  // The socket that is used to communicate with the client.

  std::atomic_bool  d_alive;
  // Whether this client session is alive or not.
  
  std::atomic_bool  d_running;
  // Whether this client session is running or not.
  
  std::thread       d_thread;
  // The thread that runs the request processing loop.
  
  CallBack          d_requestHandler;
  // The request handler that is used to handle client requests.

  FileInputStream   d_inputStream;
  // The file input stream over d_socket.

  FileOutputStream  d_outputStream;
  // The file output stream over d_socket.

  void threadLoop();
  // The request processing loop.
  
 public:
  KVClientSession(int             clientId,
		  int             socket,
		  const CallBack& requestHandler);
  // Construct a client session out of the client id, socket and
  // request handler. Note that the request handler is executed
  // on the client session's internal thread.

  ~KVClientSession();
  // Destroy this client session. Must stop the session before
  // destoying it.

  int start();
  // Start the request processing loop. Return 0 on success, non-zero
  // code on failure.

  int stop();
  // Stop the request processing loop. Return 0 on success, non-zero
  // code on failure.

  int sendResponse(int                      requestId,
		   const KVServiceResponse& response);
  // Send a response corresponding to the given request id.

  int clientId();
  // Return the client id;
  
  bool alive();
  // Whether this client session is alive or not.
};

#endif

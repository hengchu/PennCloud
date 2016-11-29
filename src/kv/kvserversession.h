#ifndef INCLUDED_KVSERVERSESSION
#define INCLUDED_KVSERVERSESSION

#include <thread>
#include <atomic>
#include <kvprotocol.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <map>

class KVServerSession {
  // This class defines a mechanism for the server sessions.
 public:
  using CallBack = std::function<void(int peerId, const KVServerMessage& msg)>;
  
 private:
  using FileInputStream = google::protobuf::io::FileInputStream;
  using FileOutputStream = google::protobuf::io::FileOutputStream;
  
  int                        d_socket;
  // The socket with which to communicate with this server.

  int                        d_peerId;
  // The id of the peer server.
  
  std::thread                d_thread;
  // The thread that runs code for communicating with this server
  // session.

  std::atomic_bool           d_running;
  // Whether this session is running or not.

  std::atomic_bool           d_alive;
  // Whether this session is still alive or not.

  std::atomic_int            d_contextId;
  // The current context id.
  
  FileInputStream            d_inputStream;
  // Input stream over d_socket.

  FileOutputStream           d_outputStream;
  // Output stream over d_socket;

  std::map<int, CallBack>    d_outstandingRequests;
  // A map from context id to outstanding request callbacks.

  std::mutex                 d_outstandingRequestsLock;
  // Lock to protect the outstanding request map.

  CallBack                   d_serverRequestHandler;
  // The request handler used to handle peer server's requests.
  
  void threadLoop();
  // The main loop of the server session.
  
 public:  
  KVServerSession(int             socket,
		  int             peerId,
		  const CallBack& callback);
  // Create a server session with given socket. The session now owns
  // this socket and is responsible for closing it.
  // It also takes a callback that is the request handler for handling
  // incoming requests from this peer server. Note that the callback
  // is invoked on this server session's internal thread.

  ~KVServerSession();
  // Destroy this session.

  bool alive();
  // Return whether this session is still alive or not.
  
  int start();
  // Start this session. Return 0 on success, non-zero code on failure.

  int stop();
  // Stop this session. Return 0 on success, non-zero code on failure.

  int sendRequest(const KVServerMessage& message,	   
		  const CallBack&        callback = CallBack());
  // Send the given message and invoke the optional callback when a
  // response is received. Note that the CallBack is invoked on the
  // internal thread of the server session!
  // Return 0 on success, non-zero code on send failure.

  int sendResponse(int                    contextId,
		   const KVServerMessage& message);
  // Send the given message as a response. The contextId parameter
  // must be the same as the one retrieved from the corresponding
  // request.
};

#endif

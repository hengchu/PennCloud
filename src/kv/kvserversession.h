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
  using CallBack = std::function<void(const KVServerMessage&)>;
  
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
  
  void threadLoop();
  // The main loop of the server session.
  
 public:  
  KVServerSession(int socket, int peerId);
  // Create a server session with given socket. The session now owns
  // this socket and is responsible for closing it.

  ~KVServerSession();
  // Destroy this session.

  bool alive();
  // Return whether this session is still alive or not.
  
  int start();
  // Start this session. Return 0 on success, non-zero code on failure.

  int stop();
  // Stop this session. Return 0 on success, non-zero code on failure.

  int send(const KVServerMessage& message,
	   const CallBack&        callback = CallBack());
  // Send the given message and invoke the optional callback when a
  // response is received. Note that the CallBack is invoked on the
  // internal thread of the server session!
  // Return 0 on success, non-zero code on send failure.
};

#endif

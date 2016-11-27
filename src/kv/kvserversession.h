#ifndef INCLUDED_KVSERVERSESSION
#define INCLUDED_KVSERVERSESSION

#include <thread>
#include <atomic>
#include <google/protobuf/io/zero_copy_stream_impl.h>

class KVServerSession {
  // This class defines a mechanism for the server sessions.
 private:
  using FileInputStream = google::protobuf::io::FileInputStream;
  
  int                        d_socket;
  // The socket with which to communicate with this server.

  std::thread                d_thread;
  // The thread that runs code for communicating with this server
  // session.

  std::atomic_bool           d_running;
  // Whether this session is running or not.

  FileInputStream            d_inputStream;
  // Input stream over d_socket.
  
  void threadLoop();
  // The main loop of the server session.
  
 public:
  KVServerSession(int socket);
  // Create a server session with given socket.

  int start();
  // Start this session. Return 0 on success, non-zero code on failure.

  int stop();
  // Stop this session. Return 0 on success, non-zero code on failure.
};

#endif

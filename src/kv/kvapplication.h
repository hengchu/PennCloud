#ifndef INCLUDED_KVAPPLICATION
#define INCLUDED_KVAPPLICATION

#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <kvconfig.pb.h>

class KVApplication {
  // This class defines the main mechanism that is the application
  // loop of kv storage service.

  std::atomic_bool              d_running;
  // Whether this application is currently running.

  KVConfiguration               d_config;
  // The configuration of the entire system.

  int                           d_serverId;
  // The id of this server.
  
  int                           d_socket;
  // The socket that the application listens on.

  sockaddr_in                   d_serverAddr;
  // The socket address of this server.

 private:
  void startListening();
  // Start listening for incoming connection.
  
 public:
  KVApplication(const KVConfiguration& config,
		int                    serverId);
  // Create an application on the given port number.

  ~KVApplication();
  // Destroy this application.
  
  int start();
  // Start the application. Returns 0 on success, a non-zero error
  // code on failure.

  int stop();
  // Stop the application. Returns 0 on success, a non-zero error code
  // on failure.
};

#endif

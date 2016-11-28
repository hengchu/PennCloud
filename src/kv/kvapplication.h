#ifndef INCLUDED_KVAPPLICATION
#define INCLUDED_KVAPPLICATION

#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <kvconfig.pb.h>
#include <map>

#ifndef INCLUDED_KVTCPCLUSTER
#include <kvtcpcluster.h>
#endif

class KVApplication {
  // This class defines the main mechanism that is the application
  // loop of kv storage service.

  std::atomic_bool                 d_running;
  // Whether this application is currently running.

  KVConfiguration                  d_config;
  // The configuration of the entire system.

  int                              d_serverId;
  // The id of this server.
  
  int                              d_clientSocket;
  // The socket that the application binds the client listening
  // address to.

  sockaddr_in                      d_clientAddr;
  // The address to bind to the client socket.

  KVTCPCluster                     d_cluster;
  
  void listenForClients();
  // Listen for incoming client connections.
  
 public:
  KVApplication(const KVConfiguration& config,
		int                    serverId);
  // Create an application on the given port number.

  ~KVApplication();
  // Destroy this application.
  
  int start();
  // Start the application. Return non-zero error code on
  // failure. Does NOT return if application is successfully started.
  // AKA the application runs on the calling thread.

  int stop();
  // Stop the application. Returns 0 on success, a non-zero error code
  // on failure.
};

#endif

#ifndef INCLUDED_KVAPPLICATION
#define INCLUDED_KVAPPLICATION

#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <kvconfig.pb.h>
#include <map>
#include <kvserversession.h>

class KVApplication {
  // This class defines the main mechanism that is the application
  // loop of kv storage service.

  std::atomic_bool                 d_running;
  // Whether this application is currently running.

  KVConfiguration                  d_config;
  // The configuration of the entire system.

  int                              d_serverId;
  // The id of this server.
  
  int                              d_socket;
  // The socket that the application binds the server address to.

  int                              d_clientSocket;
  // The socket that the application binds the client listening
  // address to.

  sockaddr_in                      d_serverAddr;
  // The address to bind to the server socket.

  sockaddr_in                      d_clientAddr;
  // The address to bind ot the client socket.
  
  std::map<int, KVServerSession *> d_servers;
  // A map from server id to server sessions. The sessions are owned
  // and must be freed upon destruction.

  void makeConnectionWithServer(int serverId);
  // Make connection with the given server.
  
  void makeConnections();
  // Make connections to other servers.

  void listenForServers();
  // Listen for incoming server connections.

  void listenForClients();
  // Listen for incoming client connections.
  
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

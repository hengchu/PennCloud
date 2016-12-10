#ifndef INCLUDED_MAILAPI
#define INCLUDED_MAILAPI

#include <pop3messages.pb.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <protoutil.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <memory>
#include <kvconfig.pb.h>

class MailSession {
  // This class implements a mechanism for communication with a KV
  // server and send request/receive response from it.

  using FileInputStream  = google::protobuf::io::FileInputStream;
  using FileOutputStream = google::protobuf::io::FileOutputStream;
  
  int                         d_socket;
  // The socket that is used to connect with the server.

  sockaddr_in                 d_serverAddr;
  // The address of the server.

  int                         d_requestId;
  // The next request id.

  std::unique_ptr<FileInputStream>
                              d_inputStream_up;
  // The input stream over d_socket.

  std::unique_ptr<FileOutputStream>
                              d_outputStream_up;
  // The output stream over d_socket.

 public:

  MailSession(const std::string& serverAddr,
	    int                port);
  // Create a session.

  ~MailSession();
  // Destroy the session. Must call disconnect() before this happens.

  MailSession(const MailSession& other) = delete;
  MailSession& operator=(const MailSession& other) = delete;
  // NOT IMPLEMENTED.

  int connect();
  // Initiate the connection. Blocks until either the session is
  // connected to the server, or returns a non-zero code for failure.

  int request(webmail::WebmailServiceResponse       *response,
	      const webmail::WebmailServiceRequest&  request);
  // Send the request to the server, blocks until a response is
  // received.  Or until an error occurs and a non-zero error code is
  // returned.

  int disconnect();
  // Shutdown the session.
};

#endif

#include <storageapi.h>
#include <logutil.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sockutil.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include <cassert>
#include <fstream>
#include <google/protobuf/text_format.h>

using namespace storage;

StorageSession::StorageSession(const std::string& serverAddr,
		                           int                   port)
  : d_socket(-1)
  , d_serverAddr()
  , d_requestId(0)
  , d_inputStream_up()
  , d_outputStream_up()
{
  std::memset(&d_serverAddr, 0, sizeof(d_serverAddr));

  d_serverAddr.sin_family = AF_INET;
  d_serverAddr.sin_port   = htons(port);

  int rc = inet_pton(AF_INET,
		     serverAddr.c_str(),
		     &(d_serverAddr.sin_addr));

  if (1 != rc) {
    LOG_ERROR << "Failed to parse server IP address = "
	      << serverAddr
	      << LOG_END;

    _exit(1);
  }
}

StorageSession::~StorageSession()
{
  // NOTHING.
}

int
StorageSession::connect()
{
  d_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (d_socket < 0) {
    LOG_ERROR << "Failed to create socket, error = "
	      << std::strerror(errno)
	      << LOG_END;
    return -1;
  }

  d_inputStream_up.reset(new FileInputStream(d_socket));
  d_outputStream_up.reset(new FileOutputStream(d_socket));

  int rc = ::connect(d_socket,
		     reinterpret_cast<sockaddr *>(&d_serverAddr),
		     sizeof(d_serverAddr));

  if (0 != rc) {
    LOG_ERROR << "Failed to connect to server, error = "
	      << std::strerror(errno)
	      << LOG_END;
    return -1;
  }

  LOG_INFO << "Established connection with server on address = "
	   << SockUtil::sockAddrToString(d_serverAddr)
	   << LOG_END;
  
  return 0;
}

int
StorageSession::disconnect()
{
  int rc = close(d_socket);

  if (0 != rc) {
    LOG_ERROR << "Failed to close socket, error = "
	      << std::strerror(errno)
	      << LOG_END;
    return -1;
  }

  return 0;
}

int
StorageSession::request(StorageServiceResponse       *response,
		   const StorageServiceRequest&  request)
{
  bool success = ProtoUtil::writeDelimitedTo(request,
					     d_outputStream_up.get());

  if (!success) {
    LOG_ERROR << "Failed to write to server."
	      << LOG_END;
    return -1;
  }

  d_outputStream_up->Flush();

  success = ProtoUtil::readDelimitedFrom(d_inputStream_up.get(),
					 response);

  if (!success) {
    LOG_ERROR << "Failed to read from server."
	      << LOG_END;
    response->set_response_code(StorageResponseCode::FAILURE);
    return -1;
  }
  
  assert(response->request_id() == request.request_id());
  return 0;
}

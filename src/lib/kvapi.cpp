#include <kvapi.h>
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

using namespace kvservice;

KVSession::KVSession(const std::string& serverAddr,
		     int                port)
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

KVSession::~KVSession()
{
  // NOTHING.
}

int
KVSession::connect()
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
KVSession::disconnect()
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
KVSession::request(KVServiceResponse       *response,
		   const KVServiceRequest&  request)
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
    response->set_response_code(ResponseCode::SERVICE_FAIL);
    response->mutable_failure()->set_error_message("Service down. Please retry again.");
    return -1;
  }
  
  assert(response->request_id() == request.request_id());
  return 0;
}

int
KVSession::loadKVConfiguration(KVConfiguration *output,
			       const char      *path)
{
  KVConfiguration config;
  std::ifstream   configInput;
  
  configInput.open(path);
  google::protobuf::io::IstreamInputStream pConfigInput(&configInput);
  
  bool parseSuccess = google::protobuf::TextFormat::Parse(&pConfigInput,
							  &config);

  if (parseSuccess) {
    *output = config;
    return 0;
  } else {
    return -1;
  }
}

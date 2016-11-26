#include <fstream>
#include <iostream>
#include <logutil.h>
#include <kvapplication.h>
#include <kvconfig.pb.h>
#include <unistd.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

struct RunParams {
  int         d_serverId;
  // Id of the server.
  
  const char *d_configFile;
  // Configuration file path.
};

int main(int argc, char *argv[])
{
  LogUtil::enableLogging();

  RunParams params;
  params.d_serverId = -1;
  params.d_configFile = 0;
  
  int c;
  while ((c = getopt(argc, argv, "c:s:")) != -1) {
    switch (c) {
    case 'c': {
      params.d_configFile = optarg;
    } break;
    case 's': {
      params.d_serverId = atoi(optarg);
    } break;
    default:
      break;
    }
  }

  if (params.d_serverId == -1) {
    LOG_ERROR << "Please set server Id."
	      << LOG_END;
    _exit(1);
  }

  if (params.d_configFile == 0) {
    LOG_ERROR << "Please specify configuration file."
	      << LOG_END;
    _exit(1);
  }

  KVConfiguration config;
  std::ifstream   configInput;
  
  configInput.open(params.d_configFile);
  google::protobuf::io::IstreamInputStream pConfigInput(&configInput);
  
  bool parseSuccess = google::protobuf::TextFormat::Parse(&pConfigInput,
							  &config);

  if (!parseSuccess) {
    LOG_ERROR << "Failed to parse the configuration file: "
	      << std::string(params.d_configFile)
	      << LOG_END;
    _exit(1);
  }

  std::string configString;
  google::protobuf::TextFormat::PrintToString(config, &configString);

  if (params.d_serverId >= config.servers_size() || params.d_serverId < 0) {
    LOG_ERROR << "Server id = "
	      << params.d_serverId
	      << " out of bounds! (0-based index)"
	      << LOG_END;
    _exit(1);
  }

  int port = config.servers(params.d_serverId).port();

  LOG_INFO << "Starting server with config = "
	   << config.servers(params.d_serverId).DebugString()
	   << LOG_END;
  
  KVApplication app(config, params.d_serverId);
  int rc = app.start();

  if (0 != rc) {
    LOG_ERROR << "Failed to start application! "
	      << "rc = "
	      << rc
	      << LOG_END;
    _exit(1);
  }
}

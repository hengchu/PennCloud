#include <fstream>
#include <iostream>
#include <logutil.h>
#include <kvapplication.h>
#include <kvconfig.pb.h>
#include <unistd.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <kvapi.h>

struct RunParams {
  int         d_serverId;
  // Id of the server.
  
  const char *d_configFile;
  // Configuration file path.
};

int main(int argc, char *argv[])
{
  RunParams params;
  params.d_serverId = -1;
  params.d_configFile = 0;
  
  int c;
  while ((c = getopt(argc, argv, "c:s:v")) != -1) {
    switch (c) {
    case 'c': {
      params.d_configFile = optarg;
    } break;
    case 's': {
      params.d_serverId = atoi(optarg);
    } break;
    case 'v': {
      LogUtil::enableLogging();
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
  int rc = KVSession::loadKVConfiguration(&config, params.d_configFile);
  if (0 != rc) {
    LOG_ERROR << "Failed to load configuration file at: "
	      << params.d_configFile
	      << LOG_END;
    _exit(1);
  }
  
  if (params.d_serverId >= config.servers_size() || params.d_serverId < 0) {
    LOG_ERROR << "Server id = "
	      << params.d_serverId
	      << " out of bounds! (0-based index)"
	      << LOG_END;
    _exit(1);
  }
  
  KVApplication app(config, params.d_serverId);
  rc = app.start();

  if (0 != rc) {
    LOG_ERROR << "Failed to start application! "
	      << "rc = "
	      << rc
	      << LOG_END;
    _exit(1);
  }
}

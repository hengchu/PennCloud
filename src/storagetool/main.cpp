#include <iostream>
#include <fstream>
#include <storageapi.h>
#include <unistd.h>
#include <semaphore.h>
#include <storage.pb.h>
#include <string>

using namespace storage;

int main(int argc, char *argv[]) {
  std::string user;
  int id;
  std::string mode;
  std::string filename;
  std::string contents;
  std::string new_name;
  int c;
  // r -- row
  // c -- col
  // m -- mode
  // v -- value
  // p -- value path
  // o -- old value (only applies to CNP)
  while ((c = getopt(argc, argv, "m:u:f:c:n:")) != -1) {
    switch (c) {
    case 'u': {
      user = optarg;
    } break;
    case 'f': {
      filename = optarg;
    } break;
    case 'm': {
      mode = optarg;
    } break;
    case 'c': {
      contents = optarg;
    } break;
    case 'n': {
      new_name = optarg;
    }
    }
  }

  std::string serverAddr = argv[optind];
  int         port       = atoi(argv[optind+1]);

  StorageServiceRequest request;

  request.set_user(user);
  if (mode == "create") {
    auto createReq = request.mutable_create();
    createReq->set_filename(filename);
    createReq->set_contents(contents);
  } else if (mode == "mkdir") {
    auto mkdirReq = request.mutable_mkdir();
    mkdirReq->set_directory(filename);
  } else if (mode == "update") {
    auto updateReq = request.mutable_update();
    updateReq->set_filename(filename);
    updateReq->set_contents(contents);
  } else if (mode == "getdir") {
    auto getReq = request.mutable_getdir();
    getReq->set_directory(filename);
  } else if (mode == "getfile") {
    auto getReq = request.mutable_getfile();
    getReq->set_filename(filename);
  } else if (mode == "delete") {
    auto deleteReq = request.mutable_delete_();
    deleteReq->set_filename(filename);
  } else if (mode == "rename") {
    auto moveReq = request.mutable_rename();
    moveReq->set_original(filename);
    moveReq->set_new_(new_name);
  }

  StorageSession session(serverAddr, port);

  int rc = session.connect();
  
  if (0 != rc) {
    std::cerr << "Failed to connect to server."
	      << std::endl;
    return 1;
  }
  
  StorageServiceResponse response;
  rc = session.request(&response,
		       request);

  if (0 != rc) {
    std::cerr << "Failed to send request to server."
	      << std::endl;
    return 1;
  }

  rc = session.disconnect();

  if (response.response_code() == StorageResponseCode::SUCCESS) {
    if (mode == "getfile") {
      std::cout << response.getfile().contents() << std::endl;
    } else if (mode == "getdir") {
      for (int i = 0; i < response.getdir().entries_size(); i++) {
        std::cout << response.getdir().entries(i).type() << "," 
          << response.getdir().entries(i).name() << "\n";
      }
    }
  } else {    
    std::cerr << "Request failed = "
	      << response.DebugString()
	      << std::endl;
  }
  
  return 0;
}

#include <iostream>
#include <fstream>
#include <mailapi.h>
#include <unistd.h>
#include <semaphore.h>
#include <pop3messages.pb.h>
#include <string>

using namespace webmail;

int main(int argc, char *argv[]) {
  std::string user;
  int id;
  std::string mode;
  int c;
  // r -- row
  // c -- col
  // m -- mode
  // v -- value
  // p -- value path
  // o -- old value (only applies to CNP)
  while ((c = getopt(argc, argv, "u:i:m:")) != -1) {
    switch (c) {
    case 'u': {
      user = optarg;
    } break;
    case 'i': {
      id = std::stoi(optarg);
    } break;
    case 'm': {
      mode = optarg;
    } break;
    }
  }

  std::string serverAddr = argv[optind];
  int         port       = atoi(argv[optind+1]);

  WebmailServiceRequest request;

  request.set_user(user);
  if (mode == "messages") {
    auto messagesReq = request.mutable_m();
  } else if (mode == "email") {
    auto emailReq = request.mutable_e();
    emailReq->set_message_id(id);
  } else if (mode == "delete") {
    auto deleteReq = request.mutable_d();
    deleteReq->set_message_id(id);
  } 

  MailSession session(serverAddr, port);

  int rc = session.connect();
  
  if (0 != rc) {
    std::cerr << "Failed to connect to server."
	      << std::endl;
    return 1;
  }
  
  WebmailServiceResponse response;
  rc = session.request(&response,
		       request);

  if (0 != rc) {
    std::cerr << "Failed to send request to server."
	      << std::endl;
    return 1;
  }

  rc = session.disconnect();

  if (response.response_code() == WebmailResponseCode::SUCCESS) {
    if (mode == "email") {
      std::cout << response.get().message() << std::endl;
    } else if (mode == "messages") {
      for (int i = 0; i < response.m().page_size(); i++) {
        std::cout << response.m().page(i).id() << "," << response.m().page(i).from() << "\n";
      }
    }
  } else {    
    std::cerr << "Request failed = "
	      << response.DebugString()
	      << std::endl;
  }
  
  return 0;
}

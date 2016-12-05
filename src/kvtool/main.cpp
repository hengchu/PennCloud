#include <iostream>
#include <fstream>
#include <kvapi.h>
#include <unistd.h>
#include <semaphore.h>

std::string
readFile(const char *path)
{
  // open the file:
  std::ifstream file(path, std::ios::binary);

  // Stop eating new lines in binary mode!!!
  file.unsetf(std::ios::skipws);

  // get its size:
  std::streampos fileSize;

  file.seekg(0, std::ios::end);
  fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // reserve capacity
  std::string output;
  output.reserve(fileSize);

  // read the data:
  output.insert(output.begin(),
		std::istream_iterator<char>(file),
		std::istream_iterator<char>());

  return output;
}

int main(int argc, char *argv[])
{
  std::string row;
  std::string col;
  std::string mode;
  std::string value;
  std::string oldValue;
  
  int c;
  // r -- row
  // c -- col
  // m -- mode
  // v -- value
  // p -- value path
  // o -- old value (only applies to CNP)
  while ((c = getopt(argc, argv, "r:c:m:v:o:p:")) != -1) {
    switch (c) {
    case 'r': {
      row = optarg;
    } break;
    case 'c': {
      col = optarg;
    } break;
    case 'm': {
      mode = optarg;
    } break;
    case 'v': {
      value = optarg;
    } break;
    case 'p': {
      value = readFile(optarg);
    } break;
    case 'o': {
      oldValue = optarg;
    } break;
    }
  }

  std::string serverAddr = argv[optind];
  int         port       = atoi(argv[optind+1]);

  if (row.empty() || col.empty()) {
    std::cerr << "Please specify row and column." << std::endl;
    return 1;
  }

  KVServiceRequest request;

  if (mode == "put") {
    if (value.empty()) {
      std::cerr << "Please specify value." << std::endl;
      return 1;
    }
    
    auto putReq = request.mutable_put();
    putReq->set_row(row);
    putReq->set_column(col);
    putReq->set_value(value);
  } else if (mode == "get") {
    auto getReq = request.mutable_get();
    getReq->set_row(row);
    getReq->set_column(col);
  } else if (mode == "delete") {
    auto deleteReq = request.mutable_delete_();
    deleteReq->set_row(row);
    deleteReq->set_column(col);
  } else if (mode == "cnp") {
    if (oldValue.empty()) {
      std::cerr << "Please specify old value." << std::endl;
      return 1;
    }

    if (value.empty()) {
      std::cerr << "Please specify value." << std::endl;
      return 1;
    }

    auto cnpReq = request.mutable_compare_put();
    cnpReq->set_row(row);
    cnpReq->set_column(col);
    cnpReq->set_old_value(oldValue);
    cnpReq->set_new_value(value);
  }

  KVSession session(serverAddr, port);

  int rc = session.connect();
  
  if (0 != rc) {
    std::cerr << "Failed to connect to server."
	      << std::endl;
    return 1;
  }
  
  KVServiceResponse response;
  rc = session.request(&response,
		       request);

  if (0 != rc) {
    std::cerr << "Failed to send request to server."
	      << std::endl;
    return 1;
  }

  rc = session.disconnect();

  if (response.response_code() != ResponseCode::SUCCESS) {
    std::cerr << "Request failed = "
	      << response.failure().error_message()
	      << std::endl;
  } else if (mode == "get") {
    std::cout << response.get().value() << std::endl;
  }
  
  return 0;
}

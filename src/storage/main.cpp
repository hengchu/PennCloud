#include "getopt.h"
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <vector>
#include <set>
#include <mutex>
#include <csignal>
#include <chrono>
#include <ctime>
#include <fstream>
#include <protoutil.h>
#include <kvconfig.pb.h>
#include <kvapi.h>
#include <storage.pb.h>

#define GOT_CONNECTION(i) if (verbose) { cerr << "[" << i << "] New connection\n"; }
#define CLOSE_CONNECTION(i) if (verbose) { cerr << "[" << i << "] Connection closed\n"; }
#define RECEIVED_COMMAND(i, s) if (verbose) { cerr << "[" << i << "] C: " << s << "\n"; }
#define SENT_COMMAND(i, s) if (verbose) { cerr << "[" << i << "] S: " << s; }

using namespace kvservice;
using namespace std;
using namespace storage;

enum INodeElement {
	file, directory
};

struct INodeEntry {
	INodeElement type;
	int inodeValue;
	string path;

	INodeEntry() {
		type = file;
		inodeValue = 0;
		path = "";
	}

	INodeEntry(INodeElement t, int v, string p) {
		type = t;
		inodeValue = v;
		path = p;
	}
};

// Global variables

// Flag that controls whether we should print debug output or not.
bool verbose = false;
KVConfiguration config;

// Mutex that controls access to list of active threads and connections.
mutex m;
set<int> threads;
set<int> connections;

// This adds info about a new thread to the global state
void add_thread(int thread_id, int connection_id) {
	lock_guard<mutex> lock(m);
	threads.insert(thread_id);
	connections.insert(connection_id);
}

// This removes info about a new thread from the global state.
void remove_thread(int thread_id, int connection_id) {
	lock_guard<mutex> lock(m);
	connections.erase(connection_id);
}

// comm_fd is the socket thread should use.
struct echo_data {
	int thread_id;
	int comm_fd;
};

vector<string> split(const string& s, char sep) {
	vector<string> elems;
	stringstream ss;
	ss.str(s);
	string item;

	while (getline(ss, item, sep)) {
		if (!item.empty()) elems.push_back(item);
	}
	return elems;
}

map<string, INodeEntry> parse_inode(const string& s) {
	map<string, INodeEntry> entries;
	
	vector<string> lines = split(s, '\n');
	for (auto i = lines.begin(); i != lines.end(); i++) {
		char comma = i->find(',');
		if (comma == string::npos) continue;

		string type = i->substr(0, comma);
		INodeElement e;
		if (type == "FILE") {
			e = file;
		} else {
			e = directory;
		}	
		string rest = i->substr(comma + 1);
		comma = rest.find(',');
		if (comma == string::npos) continue;
		int next = stoi(rest.substr(0, comma));
		string file_name = rest.substr(comma + 1);
		INodeEntry entry(e, next, file_name);
		entries[file_name] = entry;
	}
	return entries;
}

int next_inode(string to) {
	int index = 0;

	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		int id;

		if (session.connect() != 0) {
			return -1;
		}
		KVServiceResponse response;
		
		{
			KVServiceRequest kv_r;
			GetRequest* g = kv_r.mutable_get();
			g->set_row(to);
			g->set_column("next_inode");
		
			session.request(&response, kv_r); 
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					id = stoi(response.get().value());
					break;
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();	
					return -1;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}

		{
			KVServiceRequest kv_p;
			ComparePutRequest* p = kv_p.mutable_compare_put();
			p->set_row(to);
			p->set_column("next_inode");
			p->set_old_value(to_string(id));
			p->set_new_value(to_string(id + 1));

			session.request(&response, kv_p);

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return id;
					break;
				case ResponseCode::OLD_VALUE_DIFF:
					// In this case we need to retry, but can use the same server.
					session.disconnect();
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
				case ResponseCode::INVALID:
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();
					index++;
					continue;
			}
		}
	}
	return -1;
}

StorageResponseCode 
contents_of(string user, int inode, string& contents) {
	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
				              config.servers(index).client_addr().port());
		KVServiceResponse response;

		KVServiceRequest kv_r;
		GetRequest* g = kv_r.mutable_get();
		g->set_row(user);
		g->set_column("inode" + to_string(inode));
		
		session.request(&response, kv_r); 
	
		switch (response.response_code()) {
			case ResponseCode::SUCCESS:
				contents = response.get().value();
				return StorageResponseCode::SUCCESS;
			case ResponseCode::NO_SUCH_KEY:
				return StorageResponseCode::MISSING_FILE;
			case ResponseCode::FAILURE:
			case ResponseCode::SERVICE_FAIL:
				index++;
				continue;
		}
	}

	return StorageResponseCode::FAILURE;
}

StorageResponseCode inode_of(string user, string path, int& inode) {
	vector<string> paths = split(path, '/');

	int current_inode = 0;
	for (auto i = paths.begin(); i != paths.end(); i++) {
		string contents;
		StorageResponseCode code = contents_of(user, current_inode, contents);
		if (code != StorageResponseCode::SUCCESS) return code;
		map<string, INodeEntry> entries = parse_inode(contents);
		auto entry = entries.find(*i);
		if (entry == entries.end()) {
			return MISSING_FILE;
		}
		current_inode = entry->second.inodeValue;
	}
	inode = current_inode;
	return StorageResponseCode::SUCCESS;
}

bool user_exists(string to) {
	int index = 0;

	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			GetRequest* g = kv_r.mutable_get();
			g->set_row(to);
			g->set_column("inode0");
		
			session.request(&response, kv_r); 
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return true;
				case ResponseCode::NO_SUCH_KEY:
					return false;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}
	}
	return false;
}

// This is the MAIN function for the thread.
void* storage_main(void* arg) {
	struct echo_data* data = (struct echo_data*) arg;

	google::protobuf::io::ZeroCopyInputStream* is = new google::protobuf::io::FileInputStream(data->comm_fd);
	google::protobuf::io::FileOutputStream* os = new google::protobuf::io::FileOutputStream(data->comm_fd);
	StorageServiceRequest message;
	
	while(true) {
		bool got_message = ProtoUtil::readDelimitedFrom(is, &message);
		if (!got_message) {
			cout << "Error reading from socket.";
			close(data->comm_fd);
			CLOSE_CONNECTION(data->comm_fd);
			remove_thread(pthread_self(), data->comm_fd);

			pthread_exit(NULL);
		}

		switch (message.service_request_case()) {
		}

	}

	return NULL;
}

// This is the signal handler for SIGINT. It iterates over the list of
// connections, prevents the sockets from receiving further data, and
// lets the other side know the server is shutting down.
void handle_SIGINT(int signum) {
	{
		lock_guard<mutex> lock(m);
		for (set<int>::iterator i = connections.begin(); i != connections.end(); i++) {
			dprintf(*i, "%s", "-ERR Server shutting down\n");
			shutdown(*i, SHUT_RDWR);
			CLOSE_CONNECTION(*i);
		}
	}
	exit(signum);
}

void server(bool verbose, int port) {
	signal(SIGINT, handle_SIGINT);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		cerr << "Error opening socket.";
		exit(-1);
	}

	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htons(INADDR_ANY);
	server_addr.sin_port = htons(port);
	bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr));

	listen(sockfd, 10);
	while(true) {
		struct sockaddr_in clientaddr;
		socklen_t socklen = sizeof(clientaddr);
		int comm_fd = accept(sockfd, (struct sockaddr*) &clientaddr, &socklen);
		GOT_CONNECTION(comm_fd);

		pthread_t thread;
		struct echo_data* echo_data = (struct echo_data*) malloc(sizeof(struct echo_data));
		echo_data->comm_fd = comm_fd;
		int rc = pthread_create(&thread, NULL, storage_main, echo_data);
		add_thread(thread, comm_fd);
	}
}

int main(int argc, char *argv[])
{
	char o = 0;
	int port = 2500;
	while ((o = getopt(argc, argv, "avp:")) != -1) {
		switch(o) {
		case 'a':
			cerr << "*** Author: Richard Zhang (rmzhang)\n";
			exit(1);
		case 'v':
			verbose = true;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		}
	}

	if (optind < argc) {
		int error_code = KVSession::loadKVConfiguration(&config, argv[optind]);
		if (error_code != 0) {
			cerr << "Failed to load kv config file.\n";
			exit(-1);
		}
	} else {
		cerr << "No mailbox directory provided.\n";
		exit(-1);
	}

	server(verbose, port);

	return 0;
}


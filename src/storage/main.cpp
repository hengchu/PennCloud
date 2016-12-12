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

struct INodeEntry {
	DirectoryType type;
	int inodeValue;
	string path;

	INodeEntry() {
		type = DirectoryType::FILE;
		inodeValue = 0;
		path = "";
	}

	INodeEntry(DirectoryType t, int v, string p) {
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

string type_to_string(DirectoryType d) {
	switch (d) {
		case DirectoryType::FILE:
			return "FILE";
		case DirectoryType::DIRECTORY:
			return "DIRECTORY";
		default:
			return "FILE";
	}
}

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

string filename(string s) {
	vector<string> lines = split(s, '/');
	if (lines.size() == 0) return "";
	return lines.back();
}

string base_path(string s) {
	int index = s.find('/');
	if (index == string::npos) {
		return "";
	} else {
		return s.substr(0, index);
	}
}

map<string, INodeEntry> parse_inode(const string& s) {
	map<string, INodeEntry> entries;
	
	vector<string> lines = split(s, '\n');
	for (auto i = lines.begin(); i != lines.end(); i++) {
		char comma = i->find(',');
		if (comma == string::npos) continue;

		string type = i->substr(0, comma);
		DirectoryType e;
		if (type == "FILE") {
			e = DirectoryType::FILE;
		} else {
			e = DIRECTORY;
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

string to_entry_file(map<string, INodeEntry> entries) {
	stringstream ss("");
	for (auto i = entries.begin(); i != entries.end(); i++) {
		ss << type_to_string(i->second.type) << "," << i->second.inodeValue 
			<< "," << i->second.path << '\n';
	}
	return ss.str();
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
			session.disconnect();

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return id;
					break;
				case ResponseCode::OLD_VALUE_DIFF:
					// In this case we need to retry, but can use the same server.
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
				case ResponseCode::INVALID:
				case ResponseCode::NO_SUCH_KEY:
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
		
		session.connect();
		session.request(&response, kv_r); 
		session.disconnect();
	
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

StorageResponseCode inode_of(string user, string path, DirectoryType d, int& inode) {
	vector<string> paths = split(path, '/');

	int current_inode = 0;
	DirectoryType current = DIRECTORY;
	for (auto i = paths.begin(); i != paths.end(); i++) {
		if (current != DIRECTORY) {
			return MISSING_FILE;		
		}
		string contents;
		StorageResponseCode code = contents_of(user, current_inode, contents);
		if (code != StorageResponseCode::SUCCESS) return code;
		map<string, INodeEntry> entries = parse_inode(contents);
		auto entry = entries.find(*i);
		if (entry == entries.end()) {
			return MISSING_FILE;
		}
		current = entry->second.type;
		current_inode = entry->second.inodeValue;
	}
	if (current != d) {
		if (d == DIRECTORY) {
			return MISSING_DIRECTORY;
		} else {
			return MISSING_FILE; 
		}
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
			kv_r.set_request_id(0);
			GetRequest* g = kv_r.mutable_get();
			g->set_row(to);
			g->set_column("inode0");
		
			session.connect();
			session.request(&response, kv_r); 
			session.disconnect();

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

StorageResponseCode add_inode(string user, string path, int inode, string contents) {
	if (!user_exists(user)) {
		return MISSING_USER;
	}

	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			PutRequest* cpr = kv_r.mutable_put();
			cpr->set_row(user);
			cpr->set_column("inode" + to_string(inode));
			cpr->set_value(contents);

			session.connect();
			session.request(&response, kv_r);

			session.disconnect();
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return StorageResponseCode::SUCCESS;
					break;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}
	}
	return StorageResponseCode::FAILURE;
}


StorageResponseCode update_contents(string user, string path, string new_file) {
	if (!user_exists(user)) {
		return MISSING_USER;
	}

	int inode;
	StorageResponseCode c = inode_of(user, path, DirectoryType::FILE, inode);
	if (c != StorageResponseCode::SUCCESS) {
		return c;
	}

	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;
		session.connect();

		string old;
		{
			KVServiceRequest kv_r;
			GetRequest* g = kv_r.mutable_get();
			g->set_row(user);
			g->set_column("inode" + to_string(inode));
		
			session.request(&response, kv_r); 
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					old = response.get().value();
					break;
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();
					return MISSING_FILE;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					session.disconnect();
					index++;
					continue;
			}
		}

		{
			KVServiceRequest kv_r;
			ComparePutRequest* cpr = kv_r.mutable_compare_put();
			cpr->set_row(user);
			cpr->set_column("inode" + to_string(inode));
			cpr->set_old_value(old);
			cpr->set_new_value(new_file);

			session.request(&response, kv_r);

			session.disconnect();
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return StorageResponseCode::SUCCESS;
					break;
				case ResponseCode::OLD_VALUE_DIFF:
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}
	}
	return StorageResponseCode::FAILURE;
}

StorageResponseCode get_file(string user, string path, string& contents) {
	if (!user_exists(user)) {
		return MISSING_USER;
	}

	int inode;
	StorageResponseCode c = inode_of(user, path, DirectoryType::FILE, inode);
	if (c != StorageResponseCode::SUCCESS) {
		return c;
	}
	return contents_of(user, inode, contents);
}

StorageResponseCode get_dir(string user, string path, vector<DirectoryEntry>& entries) {
	if (!user_exists(user)) {
		return MISSING_USER;
	}

	int inode;
	StorageResponseCode c = inode_of(user, path, DIRECTORY, inode);
	if (c != StorageResponseCode::SUCCESS) {
		return c;
	}
	
	string contents;
	vector<DirectoryEntry> toReturn;
	c = contents_of(user, inode, contents);
	if (c != StorageResponseCode::SUCCESS) {
		return c;
	}

	map<string, INodeEntry> parsed = parse_inode(contents);

	for (auto i = parsed.begin(); i != parsed.end(); i++) {
		DirectoryEntry entry;
		entry.set_type(i->second.type);
		entry.set_name(i->second.path);
		toReturn.push_back(entry);
	}
	entries = toReturn;
	return c;
}

StorageResponseCode delete_entry(string user, string path, string name) {
	int base_inode;
	StorageResponseCode c = inode_of(user, path, DirectoryType::FILE, base_inode);
	if (c != StorageResponseCode::SUCCESS) return c;

	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		session.connect();
		KVServiceResponse response;

		string old;
		{
			KVServiceRequest kv_r;
			GetRequest* g = kv_r.mutable_get();
			g->set_row(user);
			g->set_column("inode" + to_string(base_inode));
		
			session.request(&response, kv_r); 
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					old = response.get().value();
					break;
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();
					return MISSING_FILE;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					session.disconnect();
					continue;
			}
		}

		map<string, INodeEntry> entries = parse_inode(old);
		if (entries.find(name) == entries.end()) {
			session.disconnect();
			return StorageResponseCode::SUCCESS;
		}

		entries.erase(name);
		string new_list = to_entry_file(entries);

		{
			KVServiceRequest kv_r;
			ComparePutRequest* cpr = kv_r.mutable_compare_put();
			cpr->set_row(user);
			cpr->set_column("inode" + to_string(base_inode));
			cpr->set_old_value(old);
			cpr->set_new_value(new_list);

			session.request(&response, kv_r);

			session.disconnect();
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return StorageResponseCode::SUCCESS;
					break;
				case ResponseCode::OLD_VALUE_DIFF:
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}
	}
	return StorageResponseCode::FAILURE;
}

StorageResponseCode delete_inode(string user, int inode) {
	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			kvservice::DeleteRequest* d = kv_r.mutable_delete_();
			d->set_row(user);
			d->set_column("inode" + to_string(inode));
		
			session.connect();
			session.request(&response, kv_r); 
			session.disconnect();
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
				case ResponseCode::NO_SUCH_KEY:
					return StorageResponseCode::SUCCESS;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}
	}
	return StorageResponseCode::FAILURE;
}

StorageResponseCode append_entry(string user, string path, int inode, DirectoryType type, string name) {
	int base_inode;
	StorageResponseCode c = inode_of(user, path, DIRECTORY, base_inode);
	
	if (c != StorageResponseCode::SUCCESS) {
		return c;
	}

	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		session.connect();

		string old;
		{
			KVServiceRequest kv_r;
			GetRequest* g = kv_r.mutable_get();
			g->set_row(user);
			g->set_column("inode" + to_string(base_inode));
		
			session.request(&response, kv_r); 
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					old = response.get().value();
					break;
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();
					return MISSING_FILE;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					session.disconnect();
					continue;
			}
		}

		{
			KVServiceRequest kv_r;
			ComparePutRequest* cpr = kv_r.mutable_compare_put();
			cpr->set_row(user);
			cpr->set_column("inode" + to_string(base_inode));
			cpr->set_old_value(old);

			stringstream ss("");
			ss << old << type_to_string(type) << "," << inode << "," << name << '\n';
			cpr->set_new_value(ss.str());

			session.request(&response, kv_r);

			session.disconnect();
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return StorageResponseCode::SUCCESS;
					break;
				case ResponseCode::OLD_VALUE_DIFF:
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
			}
		}
	}
	return StorageResponseCode::FAILURE;
}

StorageResponseCode new_file(string user, string path, string contents) {
	if (!user_exists(user)) return StorageResponseCode::MISSING_USER;
	
	string base = base_path(path);
	string name = filename(path);

	int next = next_inode(user);
	if (next < 0) return StorageResponseCode::FAILURE;

	StorageResponseCode c = append_entry(user, base, next, DirectoryType::FILE, name);
	if (c != StorageResponseCode::SUCCESS) return c;

	return add_inode(user, path, next, contents); 
}

StorageResponseCode new_directory(string user, string path) {
	if (!user_exists(user)) return StorageResponseCode::MISSING_USER;

	string base = base_path(path);
	string directory_name = filename(path);

	int next = next_inode(user);
	if (next < 0) return StorageResponseCode::FAILURE;

	StorageResponseCode c = append_entry(user, base, next, DIRECTORY, directory_name);
	if (c != StorageResponseCode::SUCCESS) return c;

	return add_inode(user, path, next, ""); 
}

StorageResponseCode delete_file(string user, string path) {
	if (!user_exists(user)) return StorageResponseCode::MISSING_USER;
	string base = base_path(path);
	string name = filename(path);
	int inode;
	StorageResponseCode c	= inode_of(user, path, DirectoryType::FILE, inode);
	if (c != StorageResponseCode::SUCCESS) return c;

	c = delete_entry(user, base, name);
	if (c != StorageResponseCode::SUCCESS) return c;

	return delete_inode(user, inode);
}

StorageResponseCode rename_file(string user, string path, string to) {
	if (!user_exists(user)) return StorageResponseCode::MISSING_USER;
	string name = filename(path);
	string base = base_path(path);

	int inode;
	StorageResponseCode c = inode_of(user, path, DirectoryType::FILE, inode);
	if (c != StorageResponseCode::SUCCESS) return c;
	int to_inode;
	// mostly for checking
	c = inode_of(user, to, DirectoryType::DIRECTORY, to_inode);
	if (c != StorageResponseCode::SUCCESS) return c;

	c = delete_entry(user, base, name);
	if (c != StorageResponseCode::SUCCESS) return c;

	return append_entry(user, base, inode, DirectoryType::FILE, to);
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
			close(data->comm_fd);
			CLOSE_CONNECTION(data->comm_fd);
			remove_thread(pthread_self(), data->comm_fd);

			pthread_exit(NULL);
		}
		
		StorageServiceResponse wsr;

		switch (message.service_request_case()) {
			case StorageServiceRequest::ServiceRequestCase::kCreate: 
				{
					StorageResponseCode c = new_file(message.user(),
							message.create().filename(),
							message.create().contents());
					GenericResponse* g = wsr.mutable_generic();
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}

			case StorageServiceRequest::ServiceRequestCase::kMkdir:
				{
					StorageResponseCode c = new_directory(message.user(),
							message.mkdir().directory());
					GenericResponse* g = wsr.mutable_generic();
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}

			case StorageServiceRequest::ServiceRequestCase::kUpdate:
				{
					StorageResponseCode c = update_contents(message.user(),
							message.update().filename(),
							message.update().contents());
					GenericResponse* g = wsr.mutable_generic();
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}

			case StorageServiceRequest::ServiceRequestCase::kGetdir:
				{
					vector<DirectoryEntry> entries;
					StorageResponseCode c = get_dir(message.user(),
							message.getdir().directory(),
							entries);
					GetDirectoryResponse* gd = wsr.mutable_getdir();
					for (auto i = entries.begin(); i != entries.end(); i++) {
						DirectoryEntry *entry = gd->add_entries();
						entry->set_type(i->type());
						entry->set_name(i->name());
					}
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}

			case StorageServiceRequest::ServiceRequestCase::kGetfile:
				{
					string contents;
					StorageResponseCode c = get_file(message.user(), 
							message.getfile().filename(),
							contents);
					GetFileResponse* gf = wsr.mutable_getfile();
					gf->set_contents(contents);
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}

			case StorageServiceRequest::ServiceRequestCase::kDelete:
				{
					StorageResponseCode c = delete_file(message.user(),
							message.delete_().filename());
					GenericResponse* g = wsr.mutable_generic();
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}

			case StorageServiceRequest::ServiceRequestCase::kRename:
				{
					StorageResponseCode c = rename_file(message.user(),
							message.rename().original(),
							message.rename().new_());
					GenericResponse* g = wsr.mutable_generic();
					wsr.set_user(message.user());
					wsr.set_request_id(message.request_id());
					wsr.set_response_code(c);
					break;
				}
		}

		ProtoUtil::writeDelimitedTo(wsr, os);
		os->Flush();


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
	int port = 8000;
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


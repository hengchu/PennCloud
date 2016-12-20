#include <stdlib.h>
#include <stdio.h>
#include "getopt.h"
#include <unistd.h>
#include <strings.h>
#include <iostream>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <mutex>
#include <csignal>
#include <chrono>
#include <ctime>
#include <fstream>
#include <pop3messages.pb.h>
#include <kvconfig.pb.h>
#include <kvapi.h>
#include <protoutil.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

using namespace kvservice;
using namespace webmail;
using namespace std;

#define BUFFER_SIZE 1000
#define GOT_CONNECTION(i) if (verbose) { cerr << "[" << i << "] New connection\n"; }
#define CLOSE_CONNECTION(i) if (verbose) { cerr << "[" << i << "] Connection closed\n"; }
#define RECEIVED_COMMAND(i, s) if (verbose) { cerr << "[" << i << "] C: " << s << "\n"; }
#define SENT_COMMAND(i, s) if (verbose) { cerr << "[" << i << "] S: " << s; }

// Global variables

// Flag that controls whether we should print debug output or not.
bool verbose = false;
KVConfiguration config;

// Request id (should be unique)
mutex request_id_lock;
int request_id = 0;

int next_id() {
	lock_guard<mutex> lock(request_id_lock);
	return request_id++;
}

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

list<string> get_lines(string s) {
	list<string> l;
	istringstream buffer(s);
	string token;

	while (getline(buffer, token)) {
		l.push_back(token);
	}
	return l;
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
			g->set_column("mbox");
		
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


map<int, Email> parse_messages(string mbox) {
	map<int, Email> messages;
	list<string> lines = get_lines(mbox);
	
	for (auto i = lines.begin(); i != lines.end(); i++) {
		int comma = i->find(',');
		if (comma == string::npos) {
			continue;
		}	
		Email e;
		int id = stoi(i->substr(0, comma));
		e.set_id(id);
		e.set_from(i->substr(comma + 1));
		messages[id] = e;
	}
	return messages;
}

string to_mbox(map<int, Email> emails) {
	stringstream ss("");
	
	for (auto i = emails.begin(); i != emails.end(); i++) {
		ss << i->second.id() << "\n" << i->second.from() << "\n";	
	}

	return ss.str();
}

// comm_fd is the socket thread should use.
struct echo_data {
	int thread_id;
	int comm_fd;
};

WebmailResponseCode delete_email(string user, int index) {
	int get_id = next_id();
	int delete_id = next_id();
	int server = 0;
	map<int, Email> emails;
	string contents;

	bool succeeded;
	while (server < config.servers_size()) {
		KVSession session(config.servers(server).client_addr().ip_address(),
											config.servers(server).client_addr().port());
		if (session.connect() != 0) {
			server++;
			continue;
		}
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			kv_r.set_request_id(get_id);
			GetRequest* g = kv_r.mutable_get();
			g->set_row(user);
			g->set_column("mbox");
			
			session.request(&response, kv_r);

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					contents = response.get().value();
					emails = parse_messages(contents);
					break;
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();
					return MISSING_USER;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					session.disconnect();
					server++;
					continue;
			}
		}

		{
			if (emails.find(index) == emails.end()) {
				session.disconnect();
				return WebmailResponseCode::SUCCESS;
			}
			emails.erase(emails.find(index));
			KVServiceRequest kv_p;
			kv_p.set_request_id(delete_id);

			ComparePutRequest* p = kv_p.mutable_compare_put();
			p->set_row(user);
			p->set_column("mbox");
			p->set_old_value(contents);
			p->set_new_value(to_mbox(emails));
			session.request(&response, kv_p);

			session.disconnect();
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					succeeded = true;
					break;
				case ResponseCode::OLD_VALUE_DIFF:
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					server++;
					continue;
			}
		}
	}

	if (!succeeded)	{
		return WebmailResponseCode::FAILURE;
	}

	while (server < config.servers_size()) {
		KVSession session(config.servers(server).client_addr().ip_address(),
											config.servers(server).client_addr().port());
		if (session.connect() != 0) {
			server++;
			continue;
		}
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			kv_r.set_request_id(get_id);

			kvservice::DeleteRequest* d = kv_r.mutable_delete_();
			d->set_row(user);
			d->set_column("mail" + to_string(index));
			
			session.request(&response, kv_r);

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					contents = response.get().value();
					emails = parse_messages(contents);
					break;
				case ResponseCode::NO_SUCH_KEY:
					session.disconnect();
					return MISSING_USER;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					session.disconnect();
					server++;
					continue;
			}
		}
	}
}

string get_mbox(string user) {
	int id = next_id();
	int index = 0;
	
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		if (session.connect() != 0) {
			return "";
		}
		KVServiceResponse response;

		KVServiceRequest kv_r;
		GetRequest* g = kv_r.mutable_get();
		g->set_row(user);
		g->set_column("mbox");
		kv_r.set_request_id(id);

		session.request(&response, kv_r);

		session.disconnect();
		switch (response.response_code()) {
			case ResponseCode::SUCCESS:
				return response.get().value();
			case ResponseCode::FAILURE:
			case ResponseCode::SERVICE_FAIL:
				index++;
				continue;
		}
	}

	return "";
}

map<int, Email> get_messages(string user) {
	string mbox = get_mbox(user);
	return parse_messages(mbox);
}

string get_email(string user, int id) {
	int index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		if (session.connect() != 0) {
			return "";
		}
		KVServiceResponse response;

		KVServiceRequest kv_r;
		GetRequest* g = kv_r.mutable_get();
		g->set_row(user);
		g->set_column("mail" + to_string(id));
		kv_r.set_request_id(id);

		session.request(&response, kv_r);

		session.disconnect();
		switch (response.response_code()) {
			case ResponseCode::SUCCESS:
				return response.get().value();
				break;
			case ResponseCode::FAILURE:
			case ResponseCode::SERVICE_FAIL:
				index++;
				continue;
		}
	}
	return "";
}

WebmailResponseCode create_user(string user) {
	if (user_exists(user)) return WebmailResponseCode::ALREADY_EXISTS;
	int index = 0;

	WebmailResponseCode toReturn = WebmailResponseCode::FAILURE;

	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			kv_r.set_request_id(0);
			PutRequest* p = kv_r.mutable_put();
			p->set_row(user);
			p->set_column("mbox");
		
			session.connect();
			session.request(&response, kv_r); 
			session.disconnect();

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					toReturn = WebmailResponseCode::SUCCESS;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;				
			}
		}
	}
	if (toReturn == WebmailResponseCode::FAILURE) return toReturn;
	
	toReturn = WebmailResponseCode::FAILURE;
	index = 0;
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		{
			KVServiceRequest kv_r;
			kv_r.set_request_id(0);
			PutRequest* p = kv_r.mutable_put();
			p->set_row(user);
			p->set_column("next_id");
			p->set_value("1");
		
			session.connect();
			session.request(&response, kv_r); 
			session.disconnect();

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					toReturn = WebmailResponseCode::SUCCESS;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;				
			}
		}
	}
	return toReturn;
}

// This is the MAIN function for the thread.
void* pop3(void* arg) {
	struct echo_data* data = (struct echo_data*) arg;
	
	google::protobuf::io::ZeroCopyInputStream* is = new google::protobuf::io::FileInputStream(data->comm_fd);
	google::protobuf::io::FileOutputStream* os = new google::protobuf::io::FileOutputStream(data->comm_fd);
	WebmailServiceRequest message;
	
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
			case WebmailServiceRequest::ServiceRequestCase::kE: 
				{
					string email = get_email(message.user(), message.e().message_id());
					WebmailServiceResponse wsr;
					wsr.set_request_id(message.request_id());
					wsr.set_user(message.user());
				
					if (email.empty()) {
						GenericResponse* response = wsr.mutable_generic();
						wsr.set_response_code(WebmailResponseCode::INVALID_INDEX);
					} else {
						EmailResponse* response = wsr.mutable_get();
						response->set_message(email);
						wsr.set_response_code(WebmailResponseCode::SUCCESS);
					}
					ProtoUtil::writeDelimitedTo(wsr, os);
					os->Flush();

					break;
				}

			case WebmailServiceRequest::ServiceRequestCase::kD:
				{
					WebmailServiceResponse wsr;
					GenericResponse* g = wsr.mutable_generic();
					wsr.set_request_id(message.request_id());
					wsr.set_user(message.user());

					wsr.set_response_code(delete_email(message.user(), message.d().message_id()));
					ProtoUtil::writeDelimitedTo(wsr, os);
					os->Flush();

					break;
				}

			case WebmailServiceRequest::ServiceRequestCase::kM:
				{
					map<int, Email> messages = get_messages(message.user());
					WebmailServiceResponse resp;
					MessagesResponse* response = resp.mutable_m();
	
					for (auto i = messages.begin(); i != messages.end(); i++) {
						Email* page = response->add_page();
						page->set_from(i->second.from());
						page->set_id(i->second.id());
					}

					
					resp.set_request_id(message.request_id());
					resp.set_user(message.user());
					resp.set_response_code(WebmailResponseCode::SUCCESS);
					ProtoUtil::writeDelimitedTo(resp, os);
					os->Flush();
					break;
				}
	
				case WebmailServiceRequest::ServiceRequestCase::kC:
					{
						WebmailResponseCode c = create_user(message.user());
						WebmailServiceResponse resp;
						GenericResponse* g = resp.mutable_generic();
						resp.set_response_code(c);
						ProtoUtil::writeDelimitedTo(resp, os);
						os->Flush();
						break;
					}
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
		int rc = pthread_create(&thread, NULL, pop3, echo_data);
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
			cerr << "Failed to load kv config file\n";
			exit(-1);
		}
	} else {
		cerr << "No mailbox directory provided.\n";
		exit(-1);
	}

	server(verbose, port);

	return 0;
}


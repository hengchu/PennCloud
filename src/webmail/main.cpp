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

vector<Email> parse_messages(string mbox) {
	vector<Email> messages;
	stringstream i(mbox);
	string next_line;
	string from_line;
	string next_message;

	// Handle the first message.
	do {
		getline(i, next_line);
	} while((next_line.length() < 5 || next_line.substr(0, 5) != "From ") && (!i.fail()));

	if (!i.fail()) {
		next_message = next_line + "\n";

		do {
			getline(i, next_line);
			if (next_line.length() >= 5 && next_line.substr(0, 5) == "From ") {
				Email m;
				m.set_from(from_line);
				m.set_email(next_message);
				messages.push_back(m);
				next_message = "";
				from_line = next_line + "\n";
			} else {
				next_message += next_line + "\n";
			}
		} while (!i.fail());

		Email m;
		m.set_from(from_line);
		m.set_email(next_message);
		messages.push_back(m);
	}
	return messages;
}

string to_mbox(vector<Email> emails) {
	stringstream ss("");
	
	for (auto i = emails.begin(); i != emails.end(); i++) {
		ss << i->from() << "\n" << i->email() << "\n";	
	}

	return ss.str();
}

vector<string> get_arguments(string s) {
	vector<string> l;
	istringstream buffer(s);
	string token;

	while (getline(buffer, token, ' ')) {
		l.push_back(token);
	}

	return l;
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

// comm_fd is the socket thread should use.
struct echo_data {
	int thread_id;
	int comm_fd;
};

WebmailResponseCode delete_email(string user, int index) {
	int get_id = next_id();
	int delete_id = next_id();
	int server = 0;
	vector<Email> emails;
	string contents;

	while (server < config.servers_size()) {
		KVSession session(config.servers(server).client_addr().ip_address(),
											config.servers(server).client_addr().port());
		KVServiceResponse response;

		{
			GetRequest g;
			g.set_row(user);
			g.set_column("mbox");
			KVServiceRequest kv_r;
			kv_r.set_request_id(get_id);
			kv_r.set_allocated_get(&g);

			session.request(&response, kv_r);

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					contents = response.get().value();
					emails = parse_messages(contents);
					break;
				case ResponseCode::NO_SUCH_KEY:
					return MISSING_USER;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					server++;
					continue;
			}
		}

		{
			if (index < 0 || index > emails.size()) {
				return INVALID_INDEX;
			}
			emails.erase(emails.begin() + index);
			ComparePutRequest p;
			p.set_row(user);
			p.set_column("mbox");
			p.set_old_value(contents);
			p.set_new_value(to_mbox(emails));

			KVServiceRequest kv_p;
			kv_p.set_request_id(delete_id);
			kv_p.set_allocated_compare_put(&p);
			session.request(&response, kv_p);

			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return WebmailResponseCode::SUCCESS;
				case ResponseCode::OLD_VALUE_DIFF:
					return WebmailResponseCode::CONCURRENT_CHANGE;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					// Failure here could be either that the message was changed
					// or that the RPC failed. Unfortunately it's hard to distinguish
					// right now.
					server++;
					continue;
			}
		}
	}
	return WebmailResponseCode::FAILURE;
}

vector<Email> get_messages(string user) {
	int id = next_id();
	int index = 0;
	string contents;
	
	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;

		GetRequest g;
		g.set_row(user);
		g.set_column("mbox");
		KVServiceRequest kv_r;
		kv_r.set_request_id(id);
		kv_r.set_allocated_get(&g);

		session.request(&response, kv_r);

		switch (response.response_code()) {
			case ResponseCode::SUCCESS:
				contents = response.get().value();
				break;
			case ResponseCode::FAILURE:
			case ResponseCode::SERVICE_FAIL:
				index++;
				continue;
		}
	}

	return parse_messages(contents);
}

// This is the MAIN function for the thread.
void* pop3(void* arg) {
	struct echo_data* data = (struct echo_data*) arg;

	set<int> deleted;
	
	google::protobuf::io::ZeroCopyInputStream* is = new google::protobuf::io::FileInputStream(data->comm_fd);
	google::protobuf::io::ZeroCopyOutputStream* os = new google::protobuf::io::FileOutputStream(data->comm_fd);
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
					vector<Email> messages = get_messages(message.user());
					int id = message.e().message_id();
					WebmailServiceResponse wsr;
					wsr.set_request_id(message.request_id());
					wsr.set_user(message.user());
				
					if (id < 0 || id > messages.size()) {
						GenericResponse response;
						wsr.set_response_code(WebmailResponseCode::INVALID_INDEX);
						wsr.set_allocated_generic(&response);
					} else {
						EmailResponse response;
						Email e = messages[id];
	
						response.set_message(e.from() + "\n" + e.email());
						wsr.set_response_code(WebmailResponseCode::SUCCESS);
						wsr.set_allocated_get(&response);
					}
					ProtoUtil::writeDelimitedTo(wsr, os);

					break;
				}

			case WebmailServiceRequest::ServiceRequestCase::kD:
				{
					WebmailServiceResponse wsr;
					wsr.set_request_id(message.request_id());
					wsr.set_user(message.user());
					wsr.set_allocated_generic(new GenericResponse());

					if (delete_email(message.user(), message.d().message_id())) {
						wsr.set_response_code(WebmailResponseCode::SUCCESS);
					} else {
						wsr.set_response_code(WebmailResponseCode::FAILURE);
					}
					ProtoUtil::writeDelimitedTo(wsr, os);

					break;
				}

			case WebmailServiceRequest::ServiceRequestCase::kM:
				{
					vector<Email> messages = get_messages(message.user());
					MessagesResponse response;
	
					for (auto i = messages.begin(); i != messages.end(); i++) {
						Email* page = response.add_page();
						page->set_from(i->from());
						page->set_email(i->email());
					}

					WebmailServiceResponse resp;
					resp.set_request_id(message.request_id());
					resp.set_user(message.user());
					resp.set_response_code(WebmailResponseCode::SUCCESS);
					resp.set_allocated_m(&response);
					ProtoUtil::writeDelimitedTo(resp, os);
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


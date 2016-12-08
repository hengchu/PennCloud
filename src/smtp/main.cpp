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

#define BUFFER_SIZE 1000
#define DOMAIN "localhost"
#define GOT_CONNECTION(i) if (verbose) { cerr << "[" << i << "] New connection\n"; }
#define CLOSE_CONNECTION(i) if (verbose) { cerr << "[" << i << "] Connection closed\n"; }
#define RECEIVED_COMMAND(i, s) if (verbose) { cerr << "[" << i << "] C: " << s << "\n"; }
#define SENT_COMMAND(i, s) if (verbose) { cerr << "[" << i << "] S: " << s; }

using namespace kvservice;
using namespace std;

enum smtp_state {
	HELO, FROM, TO, DATA
};

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

bool file_exists(string name) {
	if (verbose) cerr << "Checking existence of " << name << "\n";
	ifstream f(name.c_str());
	return f.good();
}

// comm_fd is the socket thread should use.
struct echo_data {
	int thread_id;
	int comm_fd;
};

void reply(int comm_fd, string str) {
	dprintf(comm_fd, "%s", str.c_str());
	SENT_COMMAND(comm_fd, str);
}

void write_message(string from, string to, string message) {
	int index = 0;

	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		KVServiceResponse response;
		string contents;

		{
			GetRequest g;
			g.set_row(to);
			g.set_column("mbox");
			KVServiceRequest kv_r;
			kv_r.set_request_id(next_id());
			kv_r.set_allocated_get(&g);
		
			session.request(&response, kv_r); 
	
			switch (response.service_response_case()) {
				case KVServiceResponse::ServiceResponseCase::kGet:
					contents = response.get().value();
					break;
				case KVServiceResponse::ServiceResponseCase::kFailure:
					index++;
					continue;
			}
		}
		
		{
			ComparePutRequest p;
			p.set_row(to);
			p.set_column("mbox");
			p.set_old_value(contents);

			auto now = std::chrono::system_clock::now();
			std::time_t now_t = std::chrono::system_clock::to_time_t(now);

			stringstream new_value(contents);
			new_value << "\nFrom " << to << std::ctime(&now_t) << "\n" << message;
			p.set_new_value(new_value.str());
			KVServiceRequest kv_p;
			kv_p.set_request_id(next_id());
			kv_p.set_allocated_compare_put(&p);
			session.request(&response, kv_p);

			switch (response.service_response_case()) {
				case KVServiceResponse::ServiceResponseCase::kGet:
					break;
				case KVServiceResponse::ServiceResponseCase::kFailure:
					// Failure here could be either that the message was changed
					// or that the RPC failed. Unfortunately it's hard to distinguish
					// right now.
					index++;
					continue;
			}
		}
	}
}

// This is the MAIN function for the thread.
void* smtp(void* arg) {
	struct echo_data* data = (struct echo_data*) arg;

	// SMTP protocol info
	smtp_state st = HELO;
	string from = "";
	set<string> to;
	string text = "";

	string str;
	dprintf(data->comm_fd, "220 localhost Server ready (Author: Richard Zhang \\ rmzhang)\n");
	while(true) {
		vector<char> buffer(BUFFER_SIZE);
		int n = read(data->comm_fd, buffer.data(), BUFFER_SIZE);
		if (n == 0) {
			cout << "Error reading from socket.";
			close(data->comm_fd);
			CLOSE_CONNECTION(data->comm_fd);
			remove_thread(pthread_self(), data->comm_fd);

			pthread_exit(NULL);
		} else {
			str.append(buffer.begin(), buffer.begin() + n);

			int newline = str.find_first_of("\r\n");
			char newline_val;
			while (newline != string::npos) {
				string next = str.substr(0, newline);
				newline_val = str[newline];
				if (newline_val == '\r') {
					if (newline + 1 <= str.length() && str[newline + 1] == '\n') {
						newline++;
					}
				}
				str = str.substr(newline + 1);

				RECEIVED_COMMAND(data->comm_fd, next);

				if (st == DATA) {
					if (next == ".") {
						for (auto i = to.begin(); i != to.end(); i++) {
							write_message(from, *i, text);
						}
						from = "";
						to.clear();
						text = "";
						st = FROM;
						reply(data->comm_fd, "250 OK\n");
					} else {
						text += next + "\n";
						reply(data->comm_fd, "250 OK\n");
					}
				} else if (strncasecmp(next.c_str(), "QUIT", 4) == 0) {
					remove_thread(pthread_self(), data->comm_fd);
					reply(data->comm_fd, "221 Goodbye!\n");

					int err_code = close(data->comm_fd);
					CLOSE_CONNECTION(data->comm_fd);

					pthread_exit(NULL);
				} else if (st == HELO && strncasecmp(next.c_str(), "HELO", 4) == 0) {
					reply(data->comm_fd, "250 Richard Zhang (rmzhang)\n");
					st = FROM;
				} else if (strncasecmp(next.c_str(), "NOOP", 4) == 0) {
					if (st == HELO) {
						reply(data->comm_fd, "503 Need HELO\n");
					} else {
						reply(data->comm_fd, "250 OK\n");
					}
				} else if (strncasecmp(next.c_str(), "RSET", 4) == 0) {
					if (st == HELO) {
						reply(data->comm_fd, "503 Need HELO\n");
					} else {
						st = FROM;
						from = "";
						to.clear();
						text = "";
						reply(data->comm_fd, "250 OK\n");
					}
				} else if (strncasecmp(next.c_str(), "MAIL FROM", 9) == 0 && st != HELO) {
					if (st == FROM) {
						from = next.substr(10);
						st = TO;
						reply(data->comm_fd, "250 OK\n");
					} else if (st == HELO) {
						reply(data->comm_fd, "503 Need HELO\n");
					} else {
						reply(data->comm_fd, "503 FROM not expected\n");
					}
				} else if (strncasecmp(next.c_str(), "RCPT TO", 7) == 0 && st != HELO) {
					if (st == TO) {
						string user = next.substr(8);
						if (user[0] == '<' && user[user.length() - 1] == '>') {
							user = user.substr(1, user.length() - 2);
						}

						int at_location = user.find('@');
						string name = user.substr(0, at_location);
						string domain = user.substr(at_location + 1);

						// Need to do checking if user exists
						if (domain == DOMAIN) {
							to.insert(name);
							reply(data->comm_fd, "250 OK\n");
						} else {
							reply(data->comm_fd, "553 forwarding not available\n");
						}	
					} else if (st == HELO) {
						reply(data->comm_fd, "503 Need HELO\n");
					} else {
						reply(data->comm_fd, "503 TO not expected\n");
					}
				} else if (strncasecmp(next.c_str(), "DATA", 4) == 0) {
					if (st == TO) {
						st = DATA;
						reply(data->comm_fd, "354 Start mail input, end with .\n");
					} else if (st == HELO) {
						reply(data->comm_fd, "503 Need HELO\n");
					} else {
						reply(data->comm_fd, "503 DATA not expected\n");
					}
				} else {
					reply(data->comm_fd, "500 Unrecognized command\n");
				}


				newline = str.find_first_of("\r\n");
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
		int rc = pthread_create(&thread, NULL, smtp, echo_data);
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


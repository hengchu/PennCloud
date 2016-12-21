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

// comm_fd is the socket thread should use.
struct echo_data {
	int thread_id;
	int comm_fd;
};

int get_id(string to) {
	int index = 0;
	int request_id = next_id();
	int compare_id = next_id();

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
			g->set_column("next_id");

			kv_r.set_request_id(request_id);
		
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
			kv_p.set_request_id(compare_id);
			ComparePutRequest* p = kv_p.mutable_compare_put();
			p->set_row(to);
			p->set_column("next_id");
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

bool user_exists(string to) {
	int index = 0;
	int id = next_id();

	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		if(session.connect() != 0) {
			return false;
		}
		KVServiceResponse response;
		
		KVServiceRequest kv_r;
		GetRequest* g = kv_r.mutable_get();
		g->set_row(to);
		g->set_column("mbox");
		kv_r.set_request_id(id);

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

void reply(int comm_fd, string str) {
	dprintf(comm_fd, "%s", str.c_str());
	SENT_COMMAND(comm_fd, str);
}

// Bool represents whether we succeeded
bool write_message(int id, string to, string message) {
	int index = 0;
	int put_id = next_id();

	while (index < config.servers_size()) {
			KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		if (session.connect() != 0) {
			index++;
			continue;
		}
		KVServiceResponse response;

		
		KVServiceRequest kv_r;
		kv_r.set_request_id(put_id);
		PutRequest* p = kv_r.mutable_put();
		p->set_row(to);
		p->set_column("mail" + to_string(id));
		p->set_value(message);

		session.request(&response, kv_r);
		
		session.disconnect();
		switch(response.response_code()) {
			case ResponseCode::SUCCESS:
				return true;
			
			case ResponseCode::FAILURE:
			case ResponseCode::SERVICE_FAIL:
				index++;
				continue;

			case ResponseCode::INVALID:
			case ResponseCode::NO_SUCH_KEY:
			default:
				return false;
		}
	}
	return false;
}

bool update_mbox(int id, string from, string to) {
	int index = 0;
	int get_id = next_id();
	int put_id = next_id();

	while (index < config.servers_size()) {
		KVSession session(config.servers(index).client_addr().ip_address(),
											config.servers(index).client_addr().port());
		if (session.connect() != 0) {
			index++;
			continue;
		}
		KVServiceResponse response;
		string contents;

		{
			KVServiceRequest kv_r;
			kv_r.set_request_id(get_id);
			
			GetRequest* g = kv_r.mutable_get();
			g->set_row(to);
			g->set_column("mbox");
					
			session.request(&response, kv_r); 
	
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					contents = response.get().value();
					break;
				case ResponseCode::NO_SUCH_KEY:
					// This shouldn't happen since we checked elsewhere
					session.disconnect();
					return false;
					break;
				case ResponseCode::SERVICE_FAIL:
					session.disconnect();
					index++;
					continue;
				case ResponseCode::FAILURE:
					session.disconnect();
					index++;
					continue;
			}
		}
		
		{
			KVServiceRequest kv_p;
			kv_p.set_request_id(put_id);

			ComparePutRequest* p = kv_p.mutable_compare_put();
			p->set_row(to);
			p->set_column("mbox");
			p->set_old_value(contents);

			auto now = std::chrono::system_clock::now();
			std::time_t now_t = std::chrono::system_clock::to_time_t(now);

			stringstream new_value("");
			new_value << contents << id << "," << from << " " << std::ctime(&now_t);
			p->set_new_value(new_value.str());
			session.request(&response, kv_p);

			session.disconnect();
			switch (response.response_code()) {
				case ResponseCode::SUCCESS:
					return true;
				case ResponseCode::OLD_VALUE_DIFF:
					// In this case we need to retry, but can use the same server.
					continue;
				case ResponseCode::FAILURE:
				case ResponseCode::SERVICE_FAIL:
					index++;
					continue;
				default:
					return false;
			}
		}
	}
	return false;
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
		if (n < 0) {
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
						bool had_error = false;
						for (auto i = to.begin(); (!had_error) && i != to.end(); i++) {
							int new_id = get_id(*i);
							if (new_id >= 0) {
								had_error = had_error || (!write_message(new_id, *i, text));
								had_error = had_error || (!update_mbox(new_id, from, *i));
							} else {
								had_error = true;
							}
						}
						from = "";
						to.clear();
						text = "";
						st = FROM;
						if (!had_error) {
							reply(data->comm_fd, "250 OK\n");
						} else {
							reply(data->comm_fd, "450 failure\n");
						}
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
							if (!user_exists(name)) {
								reply(data->comm_fd, "550 No such user\n");
							} else {
								to.insert(name);
								reply(data->comm_fd, "250 OK\n");
							}
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


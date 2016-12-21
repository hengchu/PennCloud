// Author: Zach Schutzman
// Date  : Nov 26 2016

//CHANGELOG
/*
11/26 - init
11/29 - worked on GET command
12/5 - it compiles
*/

// USAGE: ./a.out -p [PORTNO] -d [ROOTDIR]


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <fcntl.h>
#include <sstream>
#include <map>
#include <algorithm>
#include <string>
#include <kvapi.h>
#include <ctime>
#include <cstring>
#include <storageapi.h>
#include <mailapi.h>



// shorthand for maximum number of connections and buffersize, CRLF for parsing
int MAXCON = 1024;
int BUFMAX = 4096;
#define CRLF "\r\n"
char PORTNO[6];

time_t cur_time;

unsigned int cookie_gen = 111111;

std::map<int, int> cookie_pairs;

char *rootdir;
int sockfd;
int clients[1024];


int cookiegen = rand();



std::vector<std::string> valid_headers = {"cookie:","user-agent:"};
const char* ok = "200 OK";
const char* notfound = "HTTP/1.1 404 Not Found";
const char* http_v = "HTTP/1.1 ";
const char* badreq = "400 Bad Request";
const char* crlf = "\r\n";

std::string urlDecode(const std::string &SRC) {
    std::string ret;
    char ch;
    int i, ii;
    for (i=0; i<SRC.length(); i++) {
        if (int(SRC[i])==37) {
            sscanf(SRC.substr(i+1,2).c_str(), "%x", &ii);
            ch=static_cast<char>(ii);
            ret+=ch;
            i=i+2;
        } else if (SRC[i] == '+') {
        	ret+=' ';
        } else {
            ret+=SRC[i];
        }
    }
    return (ret);
}

// an auxiliary function to trim whitespace at the start and end of a string
std::string trim_string(std::string line, bool spaces){
	//printf("CALLED TRIM\n");

	if(line.size() == 0){return line;}
	if(spaces){
		while(line.at(0) == ' ' or line.at(0) == '\t' or line.at(0) == '\r' or line.at(0) == '\n'){
	
			line.erase(0,1);
			if(line.size() == 0){return line;}
		}
		//printf("DID FRONT\n");
		while(line.back() == ' ' or line.back() == '\t' or line.back() == '\r' or line.back() == '\n'){
			line.erase(line.size()-1);
			if(line.size() == 0){return line;}
		}
	}else{
		while(line.at(0) == '\r' or line.at(0) == '\n'){
			
					line.erase(0,1);
					if(line.size() == 0){return line;}
				}
				//printf("DID FRONT\n");
				while(line.back() == '\r' or line.back() == '\n'){
					line.erase(line.size()-1);
					if(line.size() == 0){return line;}
				}
	}
	//printf("DID BACK\n");
	//printf("FINISHED TRIM\n");
	return line;
}

void do_write(int sock, const std::string& data)
{
	int sent = 0;
	
	do {
		sent += write(sock, data.data() + sent, data.length() - sent);
	} while (sent != data.length());
}

void sendMail(const std::string& toAddr,
		      const std::string& fromAddr,
			  const std::string& mailBody)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	
	if (sock < 0) {
		perror("Failed to create socket for connection to SMTP server.\n");
		exit(1);
	}

	sockaddr_in smtpAddr;
	
	std::memset(&smtpAddr, 0, sizeof(smtpAddr));

	smtpAddr.sin_family = AF_INET;
	smtpAddr.sin_port   = htons(3506);

	int rc = inet_pton(AF_INET,
			     	   "127.0.0.1",
					   &(smtpAddr.sin_addr));
	
	if (rc != 1) {
		std::cerr << "Failed to parse the IP for smtp server."
				  << std::endl;
		exit(1);
	}
	
	connect(sock, reinterpret_cast<sockaddr *>(&smtpAddr), sizeof(smtpAddr));
	
	std::string helo = "HELO webserver\r\n";
	write(sock, helo.data(), helo.length());
	
	std::stringstream ss;
	ss << "MAIL FROM " << fromAddr << "\r\n";		
	write(sock, ss.str().data(), ss.str().length());
	
	ss.str("");
	ss << "RCPT TO " << toAddr << "\r\n";
	write(sock, ss.str().data(), ss.str().length());
	
	write(sock, "DATA\r\n", strlen("DATA\r\n"));
	
	ss.str("");
	ss << mailBody << "\r\n.\r\n";
	write(sock, ss.str().data(), ss.str().length());

}

//splits a string at the given delimiter
std::vector<std::string> split_string(const std::string& str, const std::string& delim)
{
	//printf("INVOKED SPLIT\n");
    std::vector<std::string> tokens;
    size_t prev = 0, pos = 0;

    std::string teststr = trim_string(str, true);
    //printf("TESTSTR SIZE IS %u\n",teststr.size());
    if(teststr.size() == 0){//printf("RETURNING EMPTY VECTOR!\n"); return tokens;
    	}
    

    do
    {
    	//printf("SPLIT DO START\n");
        pos = str.find(delim, prev);
        if (pos == std::string::npos) pos = str.length();
        std::string token = str.substr(prev, pos-prev);
        if (!token.empty()) tokens.push_back(token);
        prev = pos + delim.length();
        //printf("SPLIT DO END %s\n", token.data());
    }

    while (pos < str.length() && prev < str.length());
    //printf("SIZE OF TOKESN IS %d\n",tokens.size());
    return tokens;
}




// handles the incoming HTTP requests.  Supports GET, POST
void handle_command(std::string rawstring, int sock){
	KVSession kvs ("127.0.0.1",3500);
	if(kvs.connect() != 0){
		perror("KVS FAIL!\n");

	}
	kvservice::KVServiceRequest req;
	kvservice::KVServiceRequest req2;

	kvservice::KVServiceResponse resp,resp2;

	kvservice::GetRequest *getrq = req.mutable_get();

	kvservice::PutRequest *putrq = req2.mutable_put();
	
	
	
	
	StorageSession stor ("127.0.0.1",3504);
	if(stor.connect() != 0){
		perror("STOR FAIL\n");
		exit(1);
	}
	
	MailSession mail ("127.0.0.1",3505);
	if(mail.connect() != 0){
		perror("MAIL FAIL!\n");
		exit(1);
	}
	
	
	
//	storage::StorageServiceRequest sreq;
//	storage::StorageServiceRequest sreq2;
//	
//	storage::StorageServiceResponse sresp,sresp2;
//	
//	storage::CreateRequest *creatrq = sreq.mutable_create();
//	storage::GetFileRequest *getfrq = sreq2.mutable_getfile();


	std::vector<std::string> lines;
//
//	printf("I got characters:");
//	for(int j=0;j<rawstring.length();j++){
//		printf(" %d",(int)rawstring[j]);
//	}
	rawstring = trim_string(rawstring,false);
	lines = split_string(rawstring,"\n");
	unsigned int numlines = lines.size();
	printf("numlines %u \n",numlines);
	std::string output = "";

	char* time_s;
	std::string lasthead = "NULL";
	std::string resource, contents,post_data="";
	// parse through each line
	std::vector<std::string> ua, cook;
	unsigned int numwords;

	bool hascookie = false;
	bool _fget = false , _fpost = false;

	int fd;
	int post_req_len;

	for(int i=0;i<numlines;i++){
		//printf("%s\n",lines[i].data());
		//printf("looping %d\n",i);

		std::vector<std::string> words = split_string(lines[i]," ");
		//printf("SPLIT WORKED!\n");
		printf("WORDS 0 is %s\n",words[0].data());
		// if this is the first line:
		if(i == 0){
			if(words.size() < 3){exit(1);}
			// check if we start with a command
			//words[0] = trim_string(words[0]);
			//printf("words 0\n");
			//printf(words[0].data());

			if(words[0].compare("POST") == 0){
				_fpost = true;
				printf("WE HAVE A POST   %s   %s\n",words[1].data(),words[2].data());
			}
			else if(words[0].compare("GET") == 0){
				_fget = true;
				printf("WE HAVE A GET   %s   %s\n",words[1].data(),words[2].data());
			}


			if(_fget or _fpost){
				// check for valid version
				// check HTTP/1.0 or 1.1
				printf("HANDLING POST OR GET\n");
				words[2] = trim_string(words[2],true);
				if(strncmp(words[2].data(),"HTTP/1.0",8)!=0 and strncmp(words[2].data(),"HTTP/1.1",8)!=0){

					write(sock,http_v,strlen(http_v));
					write(sock,badreq,strlen(badreq));
					write(sock,crlf,2);
					return;

				}

				// load up the resource
				resource = trim_string(words[1],true);
				printf("RESOURCE NAME: %s\n",resource.data());
				if(resource.length() > 9){
					printf("RESOURCE SUBSTR 9: %s\n",resource.substr(0,9).data());
				}
			}
			else{return;

			}
		}



		else if(trim_string(words[0],true) == "cookie:" or trim_string(words[0], true) == "Cookie:" ){
			// std::cout << trim_string(words[1], true) << std::endl;
			
			std::string rawCookie = trim_string(words[1], true);
			int pos = rawCookie.find(";");
			printf("POS IS %d\n",pos);
			if (pos >= 0) {
				rawCookie.assign(rawCookie.begin(), rawCookie.begin() + pos);
			}
			cook.push_back(rawCookie);
			printf("GOT A COOKIE HERE IN MANUAL CHECK\n");
		}

		else if(trim_string(words[0],true) == "Content-Length:" or trim_string(words[0], true) == "content-length:" ){
			post_req_len = std::stoi(trim_string(words[1],true).data());
			printf(words[1].data());
			printf("GOT A POST WITH %d chars\n",post_req_len);
			
			if(_fpost){
				printf("READING ONE MORE LINE!\n");
				char buf;
				int count= 0;
				
				bool eol = false;
				printf("WAITING FOR %d chars\n",post_req_len);
				while(count < post_req_len){
					recv(sock,&buf,1,0);
						//if(buf != 0){
						post_data = post_data + buf;
						//printf("%d, %d\n",count,(int)buf);
						count++;
						//}
				}
				printf("READ \n%s\n",post_data.data());
			}
		}

	}
	// done reading things in, do the output
	std::string ccook;
	// lookup cookie
	if(cook.size() > 0){
		ccook = cook[0];
		printf("YOU GAVE ME A GOOD COOKIE! :: %s\n",ccook.data());

	}
	else{ // generate a cookie
		cookiegen++;
		ccook = std::to_string(cookiegen);

		// add cookie to map
	}
	// if we had a GET:
	if(_fget){
		bool common = false;
		// check if we are getting homepage:
		if(resource.compare("/") == 0 or resource.compare("/index.html") == 0 or resource.compare("/index") == 0){
			// get the index
			printf("GETTING INDEX\n");
			getrq -> set_row("index");
			getrq -> set_column("common");
			common = true;
			
					
			
		}else if(resource.compare("/upload") == 0 or resource.compare("/login") == 0 or resource.compare("/register") == 0 or resource.compare("/favicon.ico") == 0 or resource.compare("/mail/send") == 0){
			getrq ->set_row(resource);
			getrq -> set_column("common");
			printf("RESOURCE IS HERE\n");

			common = true;
		}
		
		
		if(common){
			if(kvs.request(&resp, req) != 0){
						perror("REQUEST FAIL!\n");
			}
			printf("REQUESTED!\n");
			std::cout << resp.DebugString() << std::endl;
			
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					contents = resp.get().value();
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NO",2);
					write(sock,crlf,2);
					return;
			}
		}

		else if(resource.compare("/mail") == 0){
			
			std::cout << "In /mail" << std::endl;
			
			// lookup list of my emails
			
			getrq -> set_row("clist");
			getrq -> set_column(ccook);

			if(kvs.request(&resp,req) != 0){
				perror("REQUEST FAIL! \n");
			}
			std::cout << resp.DebugString() << std::endl;

			std::string un = "";
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					un = resp.get().value();
					printf("FOUND USERNAME.  IT IS %s\n",un.data());
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NOT LOGGED IN",13);
					write(sock,crlf,2);
					return;
			}

			webmail::WebmailServiceResponse resp;
			webmail::WebmailServiceRequest  req;
			
			req.set_user(un);
			req.mutable_m();
			
			if (mail.request(&resp, req) != 0) {
				perror("Failed to contact Mail Server\n");
			}
			
			switch (resp.service_response_case()) {
			case webmail::WebmailServiceResponse::ServiceResponseCase::kM: {
				auto it = resp.m().page().begin();
				
				std::stringstream ss;
				for (; it != resp.m().page().end(); ++it) {
					ss << "id: " << it->id() << ", from: " << it->from() << "\n";
				}
				
				contents = ss.str();
			} break;
			default:
				perror("Wrong response type!\n");
			}
			
		}
		
		else if (resource.compare("/dir") == 0) {
			std::cout << "In /dir" << std::endl;
			
			// lookup list of my emails
			
			getrq -> set_row("clist");
			getrq -> set_column(ccook);

			if(kvs.request(&resp,req) != 0){
				perror("REQUEST FAIL! \n");
			}
			std::cout << resp.DebugString() << std::endl;

			std::string un = "";
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					un = resp.get().value();
					printf("FOUND USERNAME.  IT IS %s\n",un.data());
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NOT LOGGED IN",13);
					write(sock,crlf,2);
					return;
			}
			
			storage::StorageServiceRequest  req;
			storage::StorageServiceResponse resp;
			
			req.set_user(un);
			
			auto getdirreq = req.mutable_getdir();
			getdirreq->set_directory("/");
			
			if (stor.request(&resp, req) != 0) {
				perror("failed to contact storage server");
			}
			
			switch (resp.service_response_case()) {
			case storage::StorageServiceResponse::kGetdir: {
				auto it = resp.getdir().entries().begin();
				
				std::stringstream ss;
				for (; it != resp.getdir().entries().end(); ++it) {
					ss << "FileType: " << it->type() << ", FileName: " << it->name() << "\n";
				}
				
				contents = ss.str();
			} break;
			default:
				perror("Wrong response type.");
			}
		}
		
		else if (resource.substr(0,5) != "/mail"){ // not looking for a common resource: lookup pair is (cookie, resource)
			printf("NOT MAIL\n");
			// lookup username from ("clist",cookie)
			getrq -> set_row("clist");
			getrq -> set_column(ccook);

			if(kvs.request(&resp,req) != 0){
				perror("REQUEST FAIL! \n");
			}
			std::cout << resp.DebugString() << std::endl;




			// check response codes:
			std::string un = "";
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					un = resp.get().value();
					printf("FOUND USERNAME.  IT IS %s\n",un.data());
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NOT LOGGED IN",13);
					write(sock,crlf,2);
					return;
			}


			// if username not ""
			
			storage::StorageServiceRequest sreq;
			
			storage::StorageServiceResponse sresp;
			
			sreq.set_user(un); // fix this later
			
			
			
			storage::GetFileRequest *getfrq = sreq.mutable_getfile();
			
			getfrq -> set_filename(resource);
			
			
			if(stor.request(&sresp, sreq) != 0){
				perror("REQUEST FAIL!\n");
			}
			else{printf("FILE GOT!\n");}
			if(sresp.response_code() != storage::StorageResponseCode::SUCCESS){
				printf("FILE NOT FOUND!\n");
				return;
			}
			switch (sresp.service_response_case()) {
				case storage::StorageServiceResponse::ServiceResponseCase::kGetfile:{
					printf("FILE: %s\n",sresp.getfile().contents().data());
					
					contents = sresp.getfile().contents();
					
					
				} break;
				default:
					printf("WRONG RESPONSE TYPE!\n");
					return;
			}
			
		}
		
		// if resource is /mail/*
		else if(resource.substr(0,9).compare("/mail/msg") == 0){
			printf("LOOKING FOR MAIL\n");
			std::cout << resource.substr(10) << std::endl;
			
			getrq -> set_row("clist");
			getrq -> set_column(ccook);

			if(kvs.request(&resp,req) != 0){
				perror("REQUEST FAIL! \n");
			}
			std::cout << resp.DebugString() << std::endl;




			// check response codes:
			std::string un = "";
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					un = resp.get().value();
					printf("FOUND USERNAME.  IT IS %s\n",un.data());
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NOT LOGGED IN",13);
					write(sock,crlf,2);
					return;
			}
			
			
			
			
			
			
			int mailindex = atoi(resource.substr(10).data());
			
			//call mailserver
			
			webmail::WebmailServiceRequest wreq;
			webmail::WebmailServiceResponse wresp;
			
			
			wreq.set_user(un);
			
			webmail::EmailRequest *ereq = wreq.mutable_e();
			ereq->set_message_id(mailindex);
			
			if (0 != mail.request(&wresp, wreq)) {
				perror("Failed to send request.\n");
			}
			
			std::cout << "RESPONSE = " << wresp.DebugString() << std::endl;
			
			if (wresp.response_code() != webmail::WebmailResponseCode::SUCCESS) {
				perror("Failed to get mail!");
			}
			
			switch(wresp.service_response_case()) {
			case webmail::WebmailServiceResponse::kGet: {
				contents = wresp.get().message();
				std::cout << "Mail content: " << contents << std::endl;
			} break;
			default:
				perror("Wrong response type.");
			}
			
		}


		// else write headers

		write(sock,http_v,strlen(http_v));
		if(contents.compare(std::string(notfound)) == 0){
			write(sock,notfound,strlen(notfound));
			write(sock,crlf,2);
			write(sock, "Content-Length: 2",17);
			write(sock,crlf,2);
			write(sock,crlf,2);
			write(sock,"NO",2);
			write(sock,crlf,2);
			return;
		}
		else{
			write(sock,ok,strlen(ok));
			write(sock,crlf,2);


			//time
			time(&cur_time);
			time_s = ctime(&cur_time);
			write(sock,"Date: ",6);
			write(sock,time_s,strlen(time_s));
			
			//cookie
			write(sock,"Set-Cookie: ",12);
			write(sock,ccook.data(),strlen(ccook.data()));
			write(sock,crlf,2);

//			//content type
//			write(sock,"content-type: ",14);
//			write(sock,"text/html",9);
//			write(sock,crlf,2);

			// content length
			int cl = (int)contents.length();
			printf("GOT LENGTH! %d\n",cl);
			
			
			write(sock,"Content-Length: ",16);
			write(sock,std::to_string(cl).data(),strlen(std::to_string(cl).data()));
			write(sock,crlf,2);

			// blank line
			write(sock,crlf,2);

			// content
			do_write(sock,contents); //.data(),strlen(contents.data()));
			write(sock,crlf,2);

		}







	}
	else if(_fpost){
		// read one more line


		// do something based on the resource:

		//if register
		if(resource == "/register"){
			printf("parsing credentials\n");
			
			
			
		}
		if(resource == "/login" or resource == "/register"){
		// if login

		// get check un/pw combo

		std::vector<std::string> cred = split_string(post_data,"&");

		std::vector<std::string> temp = split_string(cred[0],"=");


		std::string un = temp[1];

		temp = split_string(cred[1],"=");

		std::string pw = temp[1];
		
		printf("WE HAVE UN %s and PW %s\n",un.data(),pw.data());
		
		// check if un+pw combo exists in kvs
		getrq -> set_row(pw);
		getrq -> set_column(un);
		

		if(kvs.request(&resp,req) != 0){
			perror("REQUEST FAIL! \n");
		}
		std::cout << resp.DebugString() << std::endl;
		bool login = false, reg = false;	
		if(resource == "/login"){
			login = false;
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					login = true;
	
					// put cookie in table as ("clist",cookie) <- un
	
					putrq -> set_row("clist");
					putrq -> set_column(ccook);
					putrq -> set_value(un);
					printf("LOGGED IN!\n");
	
					if(kvs.request(&resp2,req2) !=0){
						perror("REQUEST FAIL!\n");
					}
	
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					login = false;
					break;
			}
		}
		else if(resource == "/register"){
			reg = false;
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet: {
					reg = true;
					break;
				}
				
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure: {
					reg = false;
					// not registered, put in table
					putrq  -> set_column(un);
					putrq -> set_row(pw);
					putrq -> set_value("user registered");
					
					if(kvs.request(&resp2,req2) != 0){
						perror("PUT REQ FAIL\n");
					}
					
					webmail::WebmailServiceRequest wreq;
					webmail::WebmailServiceResponse wres;
					webmail::CreateUserRequest *cr = wreq.mutable_c();
					wreq.set_user(un);
					
					if (mail.request(&wres, wreq) != 0) {
						perror("MAIL FAIL\n");
					} else {
						if(webmail::WebmailResponseCode::SUCCESS != wres.response_code()) {
							perror("MAIL FAIL\n");
						}
					}
					
					storage::StorageServiceRequest sreq;
					storage::StorageServiceResponse sres;
					storage::CreateUserRequest *scr = sreq.mutable_createuser();
					sreq.set_user(un);
										
					if (stor.request(&sres, sreq) != 0) {
						perror("STORAGE FAIL\n");
					} else {
						if(storage::StorageResponseCode::SUCCESS != sres.response_code()) {
							perror("STORAGE FAIL\n");
						}
					}
					printf("REGISTERED!\n");
					break;
				}
			}
		}
		
		if(resource == "/register" or resource == "/login"){
			
			
//			//write(sock,http_v,strlen(http_v));
//			write(sock, "HTTP/1.1 303 See Other",22);
//			write(sock,crlf,2);
//			write(sock,"Location: ",10);
//			write(sock,"127.0.0.1:",10);
//			write(sock,PORTNO,strlen(PORTNO));
//			write(sock,"/index",6);
//			write(sock,crlf,2);
//			write(sock,crlf,2);
//			
//			/////
			//return;
			
			write(sock,http_v,strlen(http_v));
			write(sock,crlf,2);
			
			// write ok...
			write(sock,ok,strlen(ok));
			write(sock,crlf,2);


			//time
			time(&cur_time);
			time_s = ctime(&cur_time);
			write(sock,"Date: ",6);
			write(sock,time_s,strlen(time_s));
			
			//cookie
			write(sock,"Set-Cookie: ",12);
			write(sock,ccook.data(),strlen(ccook.data()));
			write(sock,crlf,2);

			//content type
			write(sock,"content-type: ",14);
			write(sock,"text/html",9);
			write(sock,crlf,2);
			
			// fetch the appropriate page:
			
			//if newly registered or properly logged in, splash
			if(resource == "/login" and login){contents = "<textarea>LOGIN SPLASH</textarea>";}
			if(resource == "/login" and !login){contents = "<textarea>REG SPLASH</textarea>";}
			if(resource == "/register" and !reg){contents = "<textarea>WELCOME SPLASH</textarea>";}
			if(resource == "/register" and reg){contents = "<textarea>ALREADY REGISTERD SPLASH</textarea>";}
			
			
			// else if already registered, login splash
			
			// else if login but no exist, register splash
			
			
			
			// content length
			int cl = (int)contents.length();
			printf("GOT LENGTH! %d\n",cl);
			
			
			write(sock,"content-length: ",16);
			write(sock,std::to_string(cl).data(),strlen(std::to_string(cl).data()));
			write(sock,crlf,2);

			// blank line
			write(sock,crlf,2);

			// content
			do_write(sock,contents); //.data(),strlen(contents.data()));
			write(sock,crlf,2);
			return;

		}
		
		else if(resource == "/upload"){
			printf("GOT AN UPLOAD.  SORRY SERVER!\n");
			return;
		}
		
		
		
		
		}
		else if (resource == "/sendmail"){
			
			
			
			getrq -> set_row("clist");
			getrq -> set_column(ccook);

			if(kvs.request(&resp,req) != 0){
				perror("REQUEST FAIL! \n");
			}
			std::cout << resp.DebugString() << std::endl;




			// check response codes:
			std::string un = "";
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					un = resp.get().value();
					printf("FOUND USERNAME.  IT IS %s\n",un.data());
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NOT LOGGED IN",13);
					write(sock,crlf,2);
					return;
			}
			
			
			
			
			
			
			std::cout << post_data << std::endl;
			
			std::vector<std::string> cont = split_string(post_data,"&");

			std::vector<std::string> temp = split_string(cont[0],"=");


			std::string recpt = temp[1];

			temp = split_string(cont[1],"=");

			std::string msg = temp[1];
			
			std::string fromaddr = un + "@localhost";
			
			std::cout << "Addr: " << urlDecode(recpt) << std::endl;
			std::cout << "MSG: " << urlDecode(msg) << std::endl;
			
			sendMail(urlDecode(recpt),
					 fromaddr,
				     urlDecode(msg));
			
			std::cout << "Sent mail" << std::endl;
			
			return;
			
		}
		else {//resource isnt login
			
			
			getrq -> set_row("clist");
			getrq -> set_column(ccook);

			if(kvs.request(&resp,req) != 0){
				perror("REQUEST FAIL! \n");
			}
			std::cout << resp.DebugString() << std::endl;




			// check response codes:
			std::string un = "";
			switch (resp.service_response_case()) {
				case kvservice::KVServiceResponse::ServiceResponseCase::kGet:
					un = resp.get().value();
					printf("FOUND USERNAME.  IT IS %s\n",un.data());
					break;
				case kvservice::KVServiceResponse::ServiceResponseCase::kFailure:
					contents = "no permission";
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					write(sock, "Content-Length: 2",17);
					write(sock,crlf,2);
					write(sock,crlf,2);
					write(sock,"NOT LOGGED IN",13);
					write(sock,crlf,2);
					return;
			}

			
			
			
			
			
			
			std::string postfile = "";
			std::string postfn = "";
			
			
			auto pos = post_data.find("filename=\"");
			postfn.assign(post_data.begin() + pos + strlen("filename=\"")	, post_data.end());
			pos = postfn.find("\"\r\n");
			postfn.assign(postfn.begin(),postfn.begin()+pos);
			
			std::vector<std::string> postlines = split_string(post_data,"\n");
			
			pos = post_data.find("\r\n\r\n");
			postfile.assign(post_data.begin()+pos,post_data.end());
			
			pos = postfile.find("\r\n-------");
			postfile.assign(postfile.begin()+4,postfile.begin()+pos);
			
			
			
			
			storage::StorageServiceRequest sreq;
			
			storage::StorageServiceResponse sresp;
			
			sreq.set_user(un); // fix this later
			
			
			
			storage::CreateRequest *creatrq = sreq.mutable_create();
			creatrq -> set_filename("/"+postfn);
			creatrq -> set_contents(postfile);
			
			
			
			printf("POSTFN IS: %s\n",postfn.data());
			printf("POSTFILE IS: %s\n",postfile.data());
			
			
			
			if(stor.request(&sresp, sreq) != 0){
				perror("REQUEST FAIL!\n");
			}
			else{printf("FILE PUT!\n");}
			
			
			
			std::cout << sresp.DebugString() << std::endl;
			return;
		}
						
		

	}

}





// initialize the server
void serv_init(char *port){
  struct addrinfo host, *r, *p;

  // get host info
  memset(&host,0,sizeof(host));
  host.ai_family = AF_INET;
  host.ai_socktype = SOCK_STREAM;
  host.ai_flags = AI_PASSIVE;

  if(getaddrinfo(NULL, port, &host, &r) != 0){
    perror("get addr info failed!\n");
    exit(1);
  }

  //bind the socket
  int fl =1;
  for(p = r; p!=NULL; p=p->ai_next){
    sockfd = socket(p->ai_family,p->ai_socktype,0);
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &fl, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    if(sockfd == -1){continue;}

    if(bind(sockfd,p->ai_addr, p->ai_addrlen) == 0){
      break;
    }
  }

  if(p==NULL){
    perror("bind failed!\n");
    exit(1);
  }

  freeaddrinfo(r);

  // listen for connections
  if(listen(sockfd,100000) != 0){
    perror("listening error!\n");
    exit(1);
  }
}



// handle connection
void *handle_connection(void *s){
  int sock = *(int*)s;

  std::string s_msg;

  char msg, e_msg[100000], c;
  int rec,bread,fd,counter=0;

  msg = 0;
  memset((void*)e_msg,0,100000);

	while(true){
		bool term = false;
	  // clear the message buffer


	// read from socket
	  rec = recv(sock,&msg,1,0);
	  if(rec>0){
		  // char by char
		  for(int i=0;i<rec;i++){

			  c = msg;
			  if((int)c == 0){//printf("NULL CHAR!\n");
				  
			  }
			  if((int)c!=0){

				  e_msg[counter] = c;
				  counter++;
				  //printf("we have %s\n",e_msg);
				  // check if we got a blank line (\r\n\r\n or \n\n)
				  if(counter >= 2){// client is sending \n\n
					  if(e_msg[counter] == '\n' and (e_msg[counter-1] == '\n')){
						  term = true;
					  }
				  }
				  if(counter >=4 and !term){// client is sending \r\n\r\n
					  //printf("last 4 are: %d %d %d %d \n",(int)e_msg[counter-3],(int)e_msg[counter-2],(int)e_msg[counter-1],(int)e_msg[counter]);
					  if((int)e_msg[counter] == 0 and (int)e_msg[counter-1] == 10 and (int)e_msg[counter-2] == 13 and (int)e_msg[counter-3] == 10){
						  term = true;
						  printf("FOUND EOM\n");
						  printf("HERE EMSG IS %s\n starts: %c\n",e_msg, e_msg[0]);
					  }
				  }


				  // if we've found the end of a message
				  if(term){
					  // dump it into a string and handle it
					  term = false;
					  s_msg = std::string(e_msg);
					  i = rec;
					  printf("DISPATCHING!\n e_msg is: %s\n",e_msg);
					  counter = 0;

					  handle_command(s_msg,sock);
					  msg =0;
					  memset((void*)e_msg,0,100000);
				  }
			  }
		  }

	  }
	  else{// read failed - socket closed!
		continue;
	  }
	}

	close(sock);


	return 0;
}


int main(int argc, char* argv[]){

  struct sockaddr_in clientaddr;
  socklen_t addrlen;
  char c;
  bool verbose = true;



  // default port is 10000
  // default root env is current directory
  
  strcpy(PORTNO,"10000");
  rootdir = getenv("PWD");

  //parse command line args
  for(int a=1; a<argc;a++){
	  if(strcmp(argv[a],"-v") == 0){verbose = true;}
	  if(strcmp(argv[a],"-p") == 0){strcpy(PORTNO,argv[a+1]);}
	  if(strcmp(argv[a],"-r") == 0){
		  bzero(rootdir, strlen(argv[a+1]));
		  strcpy(rootdir,argv[a+1]);
	  }
  }





  if(verbose){
    printf("started server at port %s%s%s\n root dir is %s%s%s \n", "\033[92m",PORTNO,"\033[0m","\033[92m",rootdir,"\033[0m");
  }

  // initialize client array to -1
  for(int i=0;i<MAXCON;i++){
    clients[i]=-1;
  }

  // start the server!
  serv_init(PORTNO);

  int conn = 0, *connidx;

  //accept() connections, dispatch handler thread

  while(true){

    addrlen = sizeof(clientaddr);
    clients[conn] = accept(sockfd,(struct sockaddr*)&clientaddr,&addrlen);

    if(clients[conn] < 0){
      perror("failed to accept!\n");
    } else {

      pthread_t thd;


      connidx = static_cast<int *>(malloc(16));
      *connidx = clients[conn];

      
      if(pthread_create(&thd,NULL,handle_connection,(void*)connidx)<0){
    	  perror("thread create failed!\n");
      }
    }

      // find the next available connection slot
      while(clients[conn] != -1){
	conn = (conn+1)%MAXCON;
      }
  }
    return 0;
}


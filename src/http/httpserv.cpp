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









// shorthand for maximum number of connections and buffersize, CRLF for parsing
int MAXCON = 1024;
int BUFMAX = 4096;
#define CRLF "\r\n"


unsigned int cookie_gen = 111111;

std::map<int, int> cookie_pairs;

char *rootdir;
int sockfd;
int clients[1024];

std::vector<std::string> valid_headers = {"cookie:","user-agent:"};

const char* ok = "200 OK";
const char* notfound = "404 File Not Found";
const char* http_v = "HTTP/1.0 ";
const char* badreq = "400 Bad Request";
const char* crlf = "\r\n";





// an auxiliary function to trim whitespace at the start and end of a string
std::string trim_string(std::string line){
	printf("CALLED TRIM\n");

	if(line.size() == 0){return line;}

	while(line.at(0) == ' ' or line.at(0) == '\t' or line.at(0) == '\r' or line.at(0) == '\n'){

		line.erase(0,1);
		if(line.size() == 0){return line;}
	}
	printf("DID FRONT\n");
	while(line.back() == ' ' or line.back() == '\t' or line.back() == '\r' or line.back() == '\n'){
		line.erase(line.size()-1);
		if(line.size() == 0){return line;}
	}
	printf("DID BACK\n");
	printf("FINISHED TRIM\n");
	return line;
}




std::vector<std::string> split_string(const std::string& str, const std::string& delim)
{
	printf("INVOKED SPLIT\n");
    std::vector<std::string> tokens;
    size_t prev = 0, pos = 0;

    std::string teststr = trim_string(str);
    printf("TESTSTR SIZE IS %u\n",teststr.size());
    if(teststr.size() == 0){printf("RETURNING EMPTY VECTOR!\n"); return tokens;}

    do
    {
    	printf("SPLIT DO START\n");
        pos = str.find(delim, prev);
        if (pos == std::string::npos) pos = str.length();
        std::string token = str.substr(prev, pos-prev);
        if (!token.empty()) tokens.push_back(token);
        prev = pos + delim.length();
        printf("SPLIT DO END\n");
    }

    while (pos < str.length() && prev < str.length());

    return tokens;
}









// parses a string and performs the appropriate writes to the given socket
void handle_command(std::string rawstring, int sock){


	std::vector<std::string> lines;

	printf("I got characters:");
	for(int j=0;j<rawstring.length();j++){
		printf(" %d",(int)rawstring[j]);
	}

	lines = split_string(rawstring,"\n");
	unsigned int numlines = lines.size();
	printf("numlines %ud \n",numlines);
	std::string output = "";


	std::string lasthead = "NULL";

	// parse through each line
	std::vector<std::string> ua, cook;
	unsigned int numwords;

	bool hascookie = false;

	int fd;

	for(int i=0;i<numlines;i++){
		printf("%s\n",lines[i].data());
		printf("looping %d\n",i);

		std::vector<std::string> words = split_string(lines[i]," ");
		printf("SPLIT WORKED!\n");

		// if this is the first line:
		if(i == 0){
			// check if we start with a command
			words[0] = trim_string(words[0]);
			printf("words 0\n");
			printf(words[0].data());
			if(words[0].compare("GET") == 0){
				printf("WE HAVE A GET!    %s    %s\n",words[1].data(),words[2].data());
				//we have a get command


				// check for valid version
				// check HTTP/1.0 or 1.1
				words[2] = trim_string(words[2]);
				if(strncmp(words[2].data(),"HTTP/1.0",8)!=0 and strncmp(words[2].data(),"HTTP/1.1",8)!=0){

					write(sock,http_v,strlen(http_v));
					write(sock,badreq,strlen(badreq));
					write(sock,crlf,2);
					return;

				}



				// if the specified filepath is just / - default to index
				words[1] = trim_string(words[1]);
				if(words[1].compare("/") == 0){
				// default request is /index.html
					words[1] = "/index.html";
				}
				printf("FILE PATH: %s\n",words[1].data());
				printf("CHECK1\n");
				char path[10000];
				printf("CHECK2\n");
				//load up the requested file
				// load up the path
				strcpy(path,rootdir);

				printf("strlen rootdir is %d\n",strlen(rootdir));
				strcpy(&path[strlen(rootdir)],words[1].data());
				// check if the file exists
				printf("TRYING TO OPEN\n");
				if((fd = open(path,O_RDONLY))!=1){
					// file found
					printf("OPENED FILE\n");


				}else{//couldnt find it
					// write filenotfound
					write(sock,http_v,strlen(http_v));
					write(sock,notfound,strlen(notfound));
					write(sock,crlf,2);
					return;
				}
			} else {
				write(sock,"400 No Such Command",20);
				write(sock,crlf,2);
				return;
			}
		}

		else if (words.size() > 0){//not the first line, still have stuff
			std::string currhead("NULL");
			// check if we are continuing a previous header

			if(words[0] == " " and lasthead != "NULL"){
				//part of last header
				currhead = lasthead;
			}

			else if(words[0] == "\r"){
				bool term = true;
				break;
			}
			else{
				std::transform(words[0].begin(), words[0].end(), words[0].begin(), ::tolower);
				words[0] = trim_string(words[0]);
				// check if we have a valid header
				if(std::find(valid_headers.begin(), valid_headers.end(),words[0]) != valid_headers.end()){
					lasthead = words[0];
					currhead = words[0];
				}

				else{// header not found
					// write no such header
					currhead = "NULL";
				}
			}
			// loop over the rest of the things
			for(int j=1;i<words.size();i++){
				words[j] = trim_string(words[j]);
				if(currhead == "user-agent:" and words[j].length() > 0){
					ua.push_back(words[j]);
				}
				else if(currhead == "cookie:"){
					cook.push_back(words[j]);
				}
			}


		}






	}
	// done reading things in, do the output
	std::string ccook;
	// lookup cookie
	if(cook.size() > 0){
		ccook = cook[0];

	}
	else{ // generate a cookie
		cookie_gen++;
		ccook = std::to_string(cookie_gen);

		// add cookie to map
	}

	// write headers

	// write server header

	write(sock,http_v,strlen(http_v));
	write(sock,ok,strlen(ok));
	write(sock,crlf,2);

	// write cookie
	write(sock,ccook.data(),strlen(ccook.data()));

	// write file
	int bread;
	char sendbuf[BUFMAX];
	while((bread=read(fd,sendbuf,BUFMAX))>0){
		write(sock,sendbuf,bread);
	}
	close(fd);
	write(sock,crlf,2);
	return;


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
  for(p = r; p!=NULL; p=p->ai_next){
    sockfd = socket(p->ai_family,p->ai_socktype,0);
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
        error("setsockopt(SO_REUSEADDR) failed");
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

  char msg[100000], e_msg[100000], c;
  int rec,bread,fd,counter=0;

  memset((void*)msg,0,100000);
  memset((void*)e_msg,0,100000);

	while(true){
		bool term = false;
	  // clear the message buffer


	// read from socket
	  rec = recv(sock,msg,100000,0);
	  if(rec>0){
		  // char by char
		  for(int i=0;i<rec;i++){

			  c = msg[i];
			  if(c!=0){

				  e_msg[counter] = c;
				  counter++;
				  printf("we have %s\n",e_msg);
				  // check if we got a blank line (\r\n\r\n or \n\n)
				  if(counter >= 2){// client is sending \n\n
					  if(e_msg[counter] == '\n' and (e_msg[counter-1] == '\n')){
						  term = true;
					  }
				  }
				  if(counter >=4 and !term){// client is sending \r\n\r\n
					  printf("last 4 are: %d %d %d %d \n",(int)e_msg[counter-3],(int)e_msg[counter-2],(int)e_msg[counter-1],(int)e_msg[counter]);
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
					  memset((void*)msg,0,100000);
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






// splits a string at the specified character, stores them in a vector







int main(int argc, char* argv[]){

  struct sockaddr_in clientaddr;
  socklen_t addrlen;
  char c;
  bool verbose = true;



  // default port is 10000
  // default root env is current directory
  char PORTNO[6];
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


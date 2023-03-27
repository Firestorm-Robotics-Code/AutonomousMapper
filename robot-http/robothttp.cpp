/* HTTP server for frc robots
   Simple and good with memory.
   Single threaded; polling.
   HTTP/1.1 only. */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <vector>
#include <cassert>
#include <functional>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#define BUFFER_SIZE 1024


std::vector <std::string> splitString(std::string str, char del = ' '){
	std::vector <std::string> ret;
	if (str.size() > 0){
		std::string buf;
		for (char c : str){
			if (c == del){
				ret.push_back(buf);
				buf = "";
			}
			else {
				buf += c;
			}
		}
		if (buf != ""){
			ret.push_back(buf);
		}
	}
	return ret;
}


std::string trim(std::string toTrim){
	bool retEarly = true;
	for (char c : toTrim){
		if (!isspace(c)){
			retEarly = false;
		}
	}
	if (retEarly){
		return {};
	}
	size_t start = 0;
	size_t end = toTrim.size() - 1;
	for (;isspace(toTrim[start]) && (start < toTrim.size()); start ++);
	for (;isspace(toTrim[end]) && (end >= 0); end --);
	return toTrim.substr(start, end + 1 + start);
}


struct Header {
	std::string name;
	std::string value;
};


class Response {
	int fd;
	bool sent = false;
public:
	bool close = false;
	std::string status = "200 OK";
	std::string contentType = "text/plain";
	std::vector <Header> headers;
	void send(){
		assert(!sent);
		sent = true;
		if (close){
			headers.push_back({ "Connection", "closed" });
		}
		headers.push_back({ "Content-Type", contentType });
		headers.push_back({ "Content-Length", std::to_string(body.size()) });
		headers.push_back({ "Server", "RobotServer v1.0" });
		::send(fd, "HTTP/1.1 ", 9, 0);
		::send(fd, status.c_str(), status.size(), 0);
		::send(fd, "\r\n", 2, 0);
		for (Header header : headers){
			::send(fd, header.name.c_str(), header.name.size(), 0);
			::send(fd, ": ", 2, 0);
			::send(fd, header.value.c_str(), header.value.size(), 0);
			::send(fd, "\r\n", 2, 0);
		}
		::send(fd, "\r\n", 2, 0);
		::send(fd, body.c_str(), body.size(), 0);
		if (close){
			::close(fd);
		}
	}
	
	void fail(std::string code){
		status = code;
		close = true;
		body = "";
		send();
	}

	Response(int sock){
		fd = sock;
	}
	
	~Response(){
		if (!sent){
			send();
		}
	}

	std::string body;
};


class Request {
public:
	unsigned long contentLength = 0; // content length 0 by default
	std::string url; /* No headers. for now. */
	std::string method;
	std::string body;
	bool lineMode = true;
	bool done = false;
	std::vector <Header> headers;
	enum State {
		HTTP,
		HEADER,
		BODY
	};
	
	Response* response;
	
	~Request(){
		delete response;
	}
	
	State state = HTTP;

	void line(std::string l){
		if (state == HTTP){
			std::vector <std::string> header = splitString(l);
			if (header.size() < 3){
				response -> fail("400 Bad Request");
			}
			method = header[0];
			url = header[1];
			if (trim(header[2]) != "HTTP/1.1"){
				response -> fail("400 Bad Request");
			}
			state = State::HEADER;
		}
		else if (state == State::HEADER){
			if (trim(l).size() == 0){ // empty line, allows either CRLF or just LF mode.
				state = BODY;
				lineMode = false;
				if (contentLength == 0){
					done = true;
				}
			}
			else {
				std::vector <std::string> thing = splitString(l, ':');
				for (size_t i = 0; i < thing[0].size(); i ++){
					thing[0][i] = std::tolower(thing[0][i]);
				}
				Header h = { trim(thing[0]), trim(thing[1]) };
				if (h.name == "content-length"){
					contentLength = std::stoi(h.value);
				}
				if (h.name == "connection"){
					if (h.value == "close"){ // Connection: upgrade implementation? maybe. No.
						response -> close = true;
						std::cout << "Client requests connection close." << std::endl;
					}
				}
				headers.push_back(h);
			}
		}
	}
	
	void byte(char byte){
		if (body.size() >= contentLength){
			done = true;
		}
		body += byte;
	}
};


struct Client {
	Request* req;
	int fd;
	std::string inbuf; // Line buffer. Passed to the stored request at every CRLF.
	void start(){
		req = new Request;
		req -> response = new Response(fd);
	}
	void finish(){
		delete req;
		start();
	}
	Client (int sock){
		fd = sock;
	}
	bool dead;
};


class Server {
	int sockfd;
	struct sockaddr_in address;
	std::vector <Client> clients;
	std::function <void(Request*)> requestCallback;
public:
	Server (int port, std::function <void(Request*)> cbk) : requestCallback { cbk }{
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, NULL) | O_NONBLOCK);
		perror("fcntl");
		bool reuseaddr = true;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(port);
		while (bind(sockfd, (struct sockaddr*)&address, sizeof(struct sockaddr_in)) < 0){
			perror("Bind failed");
			std::cout << "Trying again in 10 seconds." << std::endl;
			for (int x = 10; x > 0; x --){
				std::cout << x << std::endl;
				sleep(1);
			}
		}
		listen(sockfd, 32);
		std::cout << "Bound." << std::endl;
	}
	
	void Iterate () {
		int cli = accept(sockfd, NULL, NULL);
		if (cli > -1){
			fcntl(cli, F_SETFL, fcntl(cli, F_GETFL, NULL) | O_NONBLOCK);
			Client newClient { cli };
			newClient.start();
			clients.push_back(newClient);
		}
		for (size_t i = 0; i < clients.size(); i ++) {
			Client& client = clients[i];
			char buffer [BUFFER_SIZE];
			memset(buffer, 0, BUFFER_SIZE);
			if (recv(client.fd, buffer, BUFFER_SIZE, 0) == 0){
				std::cout << "Dropped client." << std::endl;
				clients.erase(clients.begin() + i);
				i --;
				continue;
			}
			for (char c : buffer) {
				if (c == 0){
					continue;
				}
				if (client.req -> lineMode){
					if (c == '\n'){
						client.req -> line(client.inbuf);
						client.inbuf = "";
					}
					else {
						client.inbuf += c;
					}
				}
				else {
					client.req -> byte(c);
				}
			}
			if (client.req -> done){
				requestCallback(client.req);
				client.finish();
			}
		}
	}
};

inline bool ends_with(std::string const & value, std::string const & ending) // THANKS, STACKOVERFLOW
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

const std::string staticDir = "test/static/";

void onRequest(Request* req){
	std::string staticPath = staticDir + req -> url;
	struct stat buffer;
	if (stat((staticPath + "/index.html").c_str(), &buffer) == 0){
		staticPath += "/index.html";
	}
	if (stat(staticPath.c_str(), &buffer) == 0){ // There is a potential exploit here relating to how POSIX directories are handled. Can you see it?
		std::ifstream stream(staticPath);
		std::stringstream buffer;
		buffer << stream.rdbuf();
		req -> response -> body = buffer.str();
		if (ends_with(staticPath, ".html")){
			req -> response -> contentType = "text/html";
		}
		else if (ends_with(staticPath, ".js")){
			req -> response -> contentType = "text/javascript";
		}
		else if (ends_with(staticPath, ".css")){
			req -> response -> contentType = "text/css";
		}
	}
	else{
		req -> response -> status = "404 Not Found";
		req -> response -> contentType = "text/html";
		req -> response -> body = "Whoopsie-diddle! You clearly don't know what page to visit! Get better n00b!<img src='https://gifsec.com/wp-content/uploads/2022/09/among-us-gif-3.gif' />";
	}
	req -> response -> send();
}

int main(){
	Server serv(8000, onRequest);
	while (true){
		serv.Iterate();
	}
	return 0;
}

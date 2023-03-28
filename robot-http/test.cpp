#include "robothttp.hpp"


const std::string staticDir = "../robot-site/";

void onRequest(Request* req){
	std::cout << req -> url << std::endl;
    if (req -> url == "/api/postRobotCommands"){
        std::cout << "Got a new set of robot commands: " << req -> body << std::endl;
        req -> response -> status = "418 I'm A Teapot";
        req -> response -> body = "... and thus, I cannot make coffee";
    }
    else{ // doesn't match an api, do static file checks
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
    }
	req -> response -> send();
}

int main(){
	Server serv(8080, onRequest);
	while (true){
		serv.Iterate();
	}
	return 0;
}
#include "CgiHandler.hpp"
#include "HttpRequest.hpp"
#include "miniserverConfig.hpp"
#include "locationConfig.hpp"
#include <iostream>
#include <map>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>

int main(int argc, char **argv)
{
    /*------------------------------------ this what the server will provide ------------------------------------*/
    
    //  hardcoded configuration will be replaced from the server and config file
    Server server("localhost", 8080, "/var/www/html", 10);
    Location location("/usr/bin/python3");

    std::map<std::string, std::string> headers;
    headers["content-type"] = "text/plain";

    std::string body = "This is the request body.";
    std::string selected_script = "test.py";

    if (argc > 1 && argv[1] && argv[1][0] != '\0')
        selected_script = argv[1];
    if (selected_script[0] == '/')
        selected_script = selected_script.substr(1);

    std::string script_uri = "/cgi-bin/" + selected_script;

    HttpRequest request(
        "POST",
        script_uri + "?foo=bar",
        "HTTP/1.1",
        headers,
        body,
        script_uri,
        "foo=bar",
        "127.0.0.1"
    );
    int core_pipe[2];
    if (pipe(core_pipe) == -1)
    {
        std::cerr << "pipe() failed" << std::endl;
        return 1;
    }
    // check to flag the cgi server do
    /*------------------------------------ this what the server will provide ------------------------------------*/

    CgiHandler cgi(request, server, location);
    bool started = cgi.startCgi(core_pipe[1]); // pass write end

    if (!started)
    {
        std::cerr << "CGI failed to start. Error code: "
                  << cgi.getErrorCode() << std::endl;
        close(core_pipe[0]);
        close(core_pipe[1]);
        return 1;
    }
    close(core_pipe[1]);
    cgi.endCgi(core_pipe[0]);
    // need to kill the childs when its done
    return 0;
}
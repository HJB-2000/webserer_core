#include "Headers/CgiStarter.hpp"
#include <unistd.h>
#include <string>

bool startCgi(
    const HttpRequest&,
    const ServerConfig&,
    const Location&,
    const std::string&,
    int result_write_fd)
{
    std::string out =
        "Status: 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "fake cgi ok\n";
    ::write(result_write_fd, out.c_str(), out.size());
    return true;
}
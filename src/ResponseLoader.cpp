#include "Headers/ResponseHandler.hpp"

#include <sys/stat.h>    
#include <sys/types.h>   
// #include <dirent.h>      
#include <fcntl.h>       
#include <unistd.h>      
#include <ctime>         
#include <cerrno>        
#include <cstring>       
#include <sstream>       
#include <iostream>      
#include <cstdio>        


void ResponseHandler::_sendErrorInternal(
    int                 code,
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    std::string body = _loadErrorPage(code, cfg);

    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << _reasonPhrase(code) << "\r\n"
        << "Server: webserv/1.0\r\n"
        << "Date: "           << _httpDate()            << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size()            << "\r\n"
        << "Connection: "     << _connectionHeader(req) << "\r\n"
        << "\r\n";
    if (req.method != "HEAD")
        oss << body;

    _appendStr(wb, oss.str());
}

std::string ResponseHandler::_loadErrorPage(
    int                 code,
    const ServerConfig& cfg) const
{
    std::map<int,std::string> ep = cfg.getErrorPageMap();
    std::map<int,std::string>::const_iterator it = ep.find(code);
    if (it != ep.end() && !it->second.empty())
    {
        int fd = ::open(it->second.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            std::string content;
            char        buf[4096];
            ssize_t     n;
            while ((n = ::read(fd, buf, sizeof(buf))) > 0)
                content.append(buf, static_cast<size_t>(n));
            ::close(fd);
            if (!content.empty())
                return content;
        }
    }
    return _builtinErrorBody(code);
}

std::string ResponseHandler::_builtinErrorBody(int code) const
{
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n<html>\n"
        << "<head><title>" << code << " " << _reasonPhrase(code) << "</title></head>\n"
        << "<body>\n"
        << "<h1>" << code << " " << _reasonPhrase(code) << "</h1>\n"
        << "<hr><p>webserv/1.0</p>\n"
        << "</body>\n</html>\n";
    return oss.str();
}

void ResponseHandler::sendError(
    int                 code,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    std::string body = _loadErrorPage(code, cfg);

    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << _reasonPhrase(code) << "\r\n"
        << "Server: webserv/1.0\r\n"
        << "Date: "           << _httpDate()   << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size()   << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    _appendStr(wb, oss.str());
}

void ResponseHandler::_appendStr(Buffer& wb, const std::string& s)
{
    wb.append(s.c_str(), s.size());
}

void ResponseHandler::_appendStr(Buffer& wb, const char* data, size_t len)
{
    wb.append(data, len);
}
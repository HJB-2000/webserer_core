#include "Headers/ResponseHandler.hpp"

#include <sys/stat.h>    
#include <sys/types.h>   
#include <dirent.h>      
#include <fcntl.h>       
#include <unistd.h>      
#include <ctime>         
#include <cerrno>        
#include <cstring>       
#include <sstream>       
#include <iostream>      
#include <cstdio>        

std::string htmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += s[i];     break;
        }
    }
    return out;
}


ResponseHandler::ResponseHandler()
{
    _mime[".html"]  = "text/html";
    _mime[".htm"]   = "text/html";
    _mime[".css"]   = "text/css";
    _mime[".js"]    = "application/javascript";
    _mime[".json"]  = "application/json";
    _mime[".xml"]   = "application/xml";
    _mime[".txt"]   = "text/plain";
    _mime[".pdf"]   = "application/pdf";
    _mime[".zip"]   = "application/zip";
    _mime[".tar"]   = "application/x-tar";
    _mime[".png"]   = "image/png";
    _mime[".jpg"]   = "image/jpeg";
    _mime[".jpeg"]  = "image/jpeg";
    _mime[".gif"]   = "image/gif";
    _mime[".ico"]   = "image/x-icon";
    _mime[".svg"]   = "image/svg+xml";
    _mime[".webp"]  = "image/webp";
    _mime[".mp4"]   = "video/mp4";
    _mime[".webm"]  = "video/webm";
    _mime[".mp3"]   = "audio/mpeg";
    _mime[".wav"]   = "audio/wav";
    _mime[".ogg"]   = "audio/ogg";
    _mime[".woff"]  = "font/woff";
    _mime[".woff2"] = "font/woff2";
    _mime[".ttf"]   = "font/ttf";
}


bool ResponseHandler::_methodAllowed(
    const std::string&              method,
    const std::vector<std::string>& allowed) const
{
    for (size_t i = 0; i < allowed.size(); ++i)
        if (allowed[i] == method)
            return true;
    return false;
}

std::string ResponseHandler::_getMimeType(const std::string& path) const
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i)
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));

    std::map<std::string,std::string>::const_iterator it = _mime.find(ext);
    if (it != _mime.end())
        return it->second;
    return "application/octet-stream";
}

std::string ResponseHandler::_httpDate() const
{
    time_t     now = std::time(NULL);
    struct tm* gmt = std::gmtime(&now);
    char       buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
    return std::string(buf);
}

std::string ResponseHandler::_reasonPhrase(int code) const
{
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 411: return "Length Required";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}

std::string ResponseHandler::_connectionHeader(const HttpRequest& req) const
{
    return req.keepAlive() ? "keep-alive" : "close";
}
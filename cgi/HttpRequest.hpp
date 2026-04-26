#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <map>
#include "miniserverConfig.hpp"
#include "locationConfig.hpp"
#include <unistd.h>
#include <cctype>

class HttpRequest 
{
    public:
        HttpRequest(
            const std::string& method, const std::string& target, const std::string& version,
            const std::map<std::string, std::string>& headers, const std::string& body,
            const std::string& path, const std::string& query, const std::string& remoteAddr
        ) : _method(method), _target(target), _version(version), _headers(headers), _body(body),
            _path(path), _query(query), _remoteAddr(remoteAddr) {}

        const std::string& getMethod() const { return _method; }
        const std::string& getTarget() const { return _target; }
        const std::string& getVersion() const { return _version; }
        const std::map<std::string, std::string>& getHeaders() const { return _headers; }
        const std::string& getBody() const { return _body; }
        const std::string& getPath() const { return _path; }
        const std::string& getQueryString() const { return _query; }
        const std::string& getRemoteAddr() const { return _remoteAddr; }
        size_t getContentLength() const { return _body.length(); }
        
        std::string getContentType() const 
        {
            if (_headers.count("content-type")) 
            {
                return _headers.at("content-type");
            }
            return "";
        }
        std::string getHeader(const std::string& key) const 
        {
            std::string lowerKey = key;
            for (size_t i = 0; i < lowerKey.length(); ++i) lowerKey[i] = std::tolower(lowerKey[i]);
            if (_headers.count(lowerKey)) 
            {
                return _headers.at(lowerKey);
            }
            return "";
        }
        std::string getScriptName(const Location& location) const 
        {
            (void)location;
            if (!_path.empty())
                return _path;

            if (_target.empty())
                return "/cgi-bin/test.py";

            size_t qmark = _target.find('?');
            if (qmark == std::string::npos)
                return _target;
            return _target.substr(0, qmark);
        }
        std::string getScriptFileName(const Server& server, const Location& location) const 
        {
            (void)server;
            (void)location;
            std::string script_name = getScriptName(location);
            const std::string cgi_prefix = "/cgi-bin/";
            std::string relative_script = script_name;

            if (relative_script.find(cgi_prefix) == 0)
                relative_script = relative_script.substr(cgi_prefix.size());
            else if (!relative_script.empty() && relative_script[0] == '/')
                relative_script = relative_script.substr(1);

            if (relative_script.empty())
                relative_script = "test.py";

            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL) 
            {
                return std::string(cwd) + "/" + relative_script;
            }
            return relative_script;
        }


    private:
        std::string _method;
        std::string _target;
        std::string _version;
        std::map<std::string, std::string> _headers;
        std::string _body;
        std::string _path;
        std::string _query;
        std::string _remoteAddr;
};

#endif

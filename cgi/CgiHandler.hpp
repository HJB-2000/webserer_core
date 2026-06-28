#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include <sys/types.h>
#include <string>
#include <vector>

#include "Headers/HttpRequest.hpp"
#include "conf/serverConfig.hpp"
#include "conf/locationConfig.hpp"


class CgiHandler
{
    public:
        CgiHandler(const HttpRequest& request, const Server& config, const Location& location, const std::string& script_path, const std::string& client_ip);
        ~CgiHandler();

        void      setBodyFd(int fd);
        bool      startCgi(int write_end);
        pid_t     getChildPid()     const;
        std::vector<std::string> buildCgiArgs(const std::string& cgi_path,
            const std::string& script_file, const std::string& query_string);

    private:
        void filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location);
        std::vector<std::string> buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const;
        bool        isEnvKeyRequired(const std::string& key) const;
        bool        validate_env_contract() const;
        void        log_env_once();

        HttpRequest&         _request;
        const Server&        _server;
        const Location&      _location;
        std::string          _script_path;
        pid_t   _child_pid;
        int     _body_fd;
        std::vector<std::string>  _meta_env;
        std::vector<char*>        _env_ptrs;
        bool                      _env_logged;
        std::string _client_ip;
};

#endif
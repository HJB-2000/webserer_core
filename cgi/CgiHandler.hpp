#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include <sys/types.h>
#include <string>
#include <vector>

#include "Headers/HttpRequest.hpp"
#include "conf/serverConfig.hpp"
#include "conf/locationConfig.hpp"

enum CgiState {
    CGI_IDLE,
    CGI_WRITING_STDIN,
    CGI_WAITING,
    CGI_READING,
    CGI_DONE,
    CGI_ERROR
};

class CgiHandler
{
    public:
        CgiHandler(const HttpRequest& request, const Server& config, const Location& location, const std::string& script_path, const std::string& client_ip);
        ~CgiHandler();

        bool startCgi(int write_end);

        CgiState           getState()        const;
        int                getErrorCode()    const;
        pid_t              getChildPid()     const;


        // Transfer ownership of the stdin write fd to the caller.
        // After this call, CgiHandler will no longer close the fd in its
        // destructor — the caller must close it.
        int  releaseStdinFd();


    private:
        void filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location);
        std::vector<std::string> buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const;
        void        close_fd(int& fd_pipe);
        bool        isEnvKeyRequired(const std::string& key) const;
        bool        validate_env_contract() const;
        void        log_env_once();

         HttpRequest&   _request;
        const Location&      _location;
        std::string          _script_path;

        pid_t   _child_pid;
        int     _cgi_in_pipe[2];

        CgiState     _state;


        int          _error_code;

        std::vector<std::string>  _meta_env;
        std::vector<char*>        _env_ptrs;
        bool                      _env_logged;
        std::string _client_ip;
};

#endif
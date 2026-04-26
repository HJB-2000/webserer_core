#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <string>
#include <vector>
#include <iostream>
#include <errno.h>

// #include "locationConfig.hpp"
// #include "miniserverConfig.hpp"
#include "HttpRequest.hpp"

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
        CgiHandler(const HttpRequest& request, const Server& config, const Location& location);
        ~CgiHandler();

        bool startCgi(int write_end);
        void endCgi(int read_end); // void for now

        

        CgiState           getState()        const;
        const std::string& getResponse()     const;
        int                getErrorCode()    const;

        int  getStdinWriteFd()  const { return cgi_in_pipe[1];  }
        // int  getStdoutReadFd()  const { return cgi_out_pipe[0]; }

        std::string _build_http_from_cgi_output(const std::string& raw) const;
    private:
        void filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location);
        std::vector<std::string> buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const;
        void        _parse_cgi_output();
        void        close_fd(int& fd_pipe);
        bool        isEnvKeyRequired(const std::string& key) const;
        bool        validate_env_contract() const;
        void        log_env_once();

        static std::string _trim(const std::string& s);
        static std::string _toLower(const std::string& s);
        static std::string _toStrInt(int n);
        static std::string _toStrSize(size_t n);
        static std::string _reason_phrase(int status_code);

        HttpRequest          _request;
        const Server&        _config_server;
        const Location&      _location;

        pid_t   _child_pid;
        int     cgi_in_pipe[2];
        // int     cgi_out_pipe[2];   // unused in new contract

        CgiState     _state;
        std::string  _output_buffer;
        size_t       _bytes_written;

        struct timeval  _start_time;
        int             _timeout_seconds;

        std::string  _response;
        int          _error_code;

        std::vector<std::string>  _meta_env;
        std::vector<char*>        _env_ptrs;
        bool                      _env_logged;
};

#endif
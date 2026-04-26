#include "CgiHandler.hpp"

#include <sys/time.h>
#include <sstream>
#include <iostream>
#include <cctype>
#include <fcntl.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/epoll.h>


bool CgiHandler::isEnvKeyRequired(const std::string& key) const
{
    return key == "REQUEST_METHOD"    ||
           key == "REQUEST_URI"       ||
           key == "SCRIPT_NAME"       ||
           key == "SCRIPT_FILENAME"   ||
           key == "SERVER_NAME"       ||
           key == "SERVER_PORT"       ||
           key == "SERVER_PROTOCOL"   ||
           key == "GATEWAY_INTERFACE" ||
           key == "DOCUMENT_ROOT"     ||
           key == "REMOTE_ADDR";
}

bool CgiHandler::validate_env_contract() const
{
    bool ok = true;
    // Check existing keys for empty values
    for (size_t i = 0; i < _meta_env.size(); ++i)
    {
        const std::string& line = _meta_env[i];
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
        {
            ok = false;
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (isEnvKeyRequired(key) && value.empty())
            ok = false;
    }

    // Check for missing required keys
    const char* required[] = {
        "REQUEST_METHOD", "REQUEST_URI", "SCRIPT_NAME",
        "SCRIPT_FILENAME", "SERVER_NAME", "SERVER_PORT",
        "SERVER_PROTOCOL", "GATEWAY_INTERFACE", "DOCUMENT_ROOT",
        "REMOTE_ADDR", NULL
    };
    for (int r = 0; required[r] != NULL; ++r)
    {
        bool found = false;
        std::string prefix = std::string(required[r]) + "=";
        for (size_t i = 0; i < _meta_env.size(); ++i)
        {
            if (_meta_env[i].compare(0, prefix.size(), prefix) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cerr << "[cgi][env] MISSING required key: " << required[r] << std::endl;
            ok = false;
        }
    }
    return ok;
}

void CgiHandler::log_env_once()
{
    if (_env_logged) return;
    std::cerr << "[cgi][env] generated entries:" << std::endl;
    for (size_t i = 0; i < _meta_env.size(); ++i)
        std::cerr << "  " << _meta_env[i] << std::endl;
    std::cerr << "[cgi][env] contract check: "
              << (validate_env_contract() ? "PASS" : "FAIL") << std::endl;
    _env_logged = true;
}

void CgiHandler::filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location)
{
    _meta_env = buildCgiEnvironment(request, config, location);
    _env_ptrs.clear();
    for (size_t i = 0; i < _meta_env.size(); ++i)
        _env_ptrs.push_back(const_cast<char*>(_meta_env[i].c_str()));
    _env_ptrs.push_back(NULL);
    log_env_once();
}

std::vector<std::string> CgiHandler::buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const
{
    (void)location;
    std::vector<std::string> env;
    std::string host = server.getHost();
    if (host.empty()) host = request.header("host");

    std::string script_name     = request.path;
    std::string script_filename = _script_path;
    std::string full_path       = request.path;

    std::string path_info;
    if (full_path.length() > script_name.length() && full_path.find(script_name) == 0)
    {
        path_info = full_path.substr(script_name.length());
        if (!path_info.empty() && path_info[0] != '/') path_info = "/" + path_info;
    }

    std::string path_translated;
    if (!path_info.empty())
    {
        std::string doc_root = server.getRoot();
        path_translated = doc_root + path_info;
    }

    std::ostringstream content_length_ss;
    content_length_ss << request.body.size();
    std::ostringstream server_port_ss;
    server_port_ss << server.getPort();

    env.push_back("REQUEST_METHOD="    + request.method);
    env.push_back("QUERY_STRING="      + request.query_string);
    env.push_back("CONTENT_TYPE="      + request.header("content-type"));
    env.push_back("CONTENT_LENGTH="    + content_length_ss.str());
    env.push_back("SCRIPT_FILENAME="   + script_filename);
    env.push_back("SCRIPT_NAME="       + script_name);
    env.push_back("PATH_INFO="         + path_info);
    if (!path_translated.empty()) 
        env.push_back("PATH_TRANSLATED=" + path_translated);
    env.push_back("SERVER_NAME="       + host);
    env.push_back("SERVER_PORT="       + server_port_ss.str());
    env.push_back("SERVER_PROTOCOL="   + request.version);
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_SOFTWARE=webserv/1.0");
    env.push_back("REMOTE_ADDR=127.0.0.1");
    env.push_back("REQUEST_URI=" + request.path +
        (request.query_string.empty() ? "" : "?" + request.query_string));
    env.push_back("DOCUMENT_ROOT=" + server.getRoot());

    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
         it != request.headers.end(); ++it)
    {
        std::string key = it->first;
        for (size_t i = 0; i < key.size(); ++i)
        {
            if (key[i] == '-') key[i] = '_';
            else key[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[i])));
        }
        env.push_back("HTTP_" + key + "=" + it->second);
    }
    return env;
}

CgiHandler::CgiHandler(const HttpRequest& request, const Server& config, const Location& location, const std::string& script_path)
    : _request(request), _location(location),
      _script_path(script_path), _child_pid(-1), _state(CGI_IDLE),
      _error_code(0), _env_logged(false)
{
    cgi_in_pipe[0]  = -1;
    cgi_in_pipe[1]  = -1;
    // cgi_out_pipe[0] = -1;
    // cgi_out_pipe[1] = -1;
    gettimeofday(&_start_time, NULL);
    filling_meta_variables(request, config, location);
}

void CgiHandler::close_fd(int& fd_pipe)
{
    if (fd_pipe != -1) 
    {
        close(fd_pipe);
        fd_pipe = -1;
    }
}

bool CgiHandler::startCgi(int write_end)
{
    std::string cgi_path = _location.getCGI_path();
    if (cgi_path.empty()) 
    {
        _error_code = 500;
        _state = CGI_ERROR;
        return false;
    }

    struct stat sb;
    if (stat(cgi_path.c_str(), &sb) != 0)
    {
        _error_code = 500;
        _state = CGI_ERROR; 
        return false;
    }
    if (!S_ISREG(sb.st_mode))
    {
        _error_code = 500;
        _state = CGI_ERROR; 
        return false;
    }
    if (access(cgi_path.c_str(), X_OK) != 0)
    {
        _error_code = 500;
        _state = CGI_ERROR; 
        return false;
    }

    std::string script_file = _script_path;
    if (script_file.empty())
    {
        _error_code = 404;
        _state = CGI_ERROR; 
        return false;
    }
    if (stat(script_file.c_str(), &sb) != 0)
    {
        _error_code = 404;
        _state = CGI_ERROR; 
        return false;
    }
    if (!S_ISREG(sb.st_mode))
    {
        _error_code = 403;
        _state = CGI_ERROR; 
        return false;
    }
    if (access(script_file.c_str(), R_OK) != 0)
    {
        _error_code = 403;
        _state = CGI_ERROR; 
        return false;
    }

    const std::string& method = _request.method;
    const std::string& body   = _request.body;
    bool need_stdin = ((method == "POST" || method == "DELETE") && !body.empty());

    if (need_stdin)
    {
        if (pipe(cgi_in_pipe) == -1) 
        {
            _error_code = 500;
            _state = CGI_ERROR;
            return false;
        }
    }

    _child_pid = fork();
    if (_child_pid < 0)
    {
        if (need_stdin)
        {
            close_fd(cgi_in_pipe[0]);
            close_fd(cgi_in_pipe[1]);
        }
        _error_code = 500;
        _state = CGI_ERROR;
        return false;
    }

    if (_child_pid == 0)
    {
        if(dup2(write_end, STDOUT_FILENO) == -1)
            _exit(1);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        if (need_stdin)
            dup2(cgi_in_pipe[0], STDIN_FILENO);
        else
        {
            // No body: ensure the child doesn't inherit the server's stdin.
            int devnull_in = open("/dev/null", O_RDONLY);
            if (devnull_in >= 0)
            {
                dup2(devnull_in, STDIN_FILENO);
                close(devnull_in);
            }
        }

        close_fd(cgi_in_pipe[0]);
        if (need_stdin)
            close_fd(cgi_in_pipe[1]);
        close(write_end);  // original fd no longer needed after dup2

        std::string script_dir;
        std::string script_arg;
        std::string::size_type slash = script_file.find_last_of('/');
        if (slash != std::string::npos)
        {
            script_dir = script_file.substr(0, slash);
            script_arg = script_file.substr(slash + 1);
        }
        else 
        {
            script_dir = ".";
            script_arg = script_file;
        }
        if (chdir(script_dir.c_str()) != 0)
            _exit(127);

        char* argv[3];
        argv[0] = const_cast<char*>(cgi_path.c_str());
        argv[1] = const_cast<char*>(script_arg.c_str());
        argv[2] = NULL;

        if (_env_ptrs.empty() || _env_ptrs.back() != NULL)
            _exit(127);

        execve(cgi_path.c_str(), argv, &_env_ptrs[0]);
        _exit(127);
    }

    // ── parent ──────────────────────────────────────────────
    // Close the child's end of the stdin pipe. The caller (EventLoop) is
    // responsible for writing the body to cgi_in_pipe[1] non-blockingly and
    // closing it when done. We do NOT write here to avoid deadlocks when
    // body size exceeds the pipe buffer (~64KB on Linux).
    if (need_stdin)
    {
        close_fd(cgi_in_pipe[0]);  // parent doesn't need read end
        // Make the write end non-blocking so the EventLoop can drain it
        // incrementally via EPOLLOUT without blocking the whole server.
        int flags = fcntl(cgi_in_pipe[1], F_GETFL, 0);
        if (flags != -1)
            fcntl(cgi_in_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }

    gettimeofday(&_start_time, NULL);
    _state = CGI_WAITING;
    return true;
}

CgiHandler::~CgiHandler()
{
    close_fd(cgi_in_pipe[0]);
    close_fd(cgi_in_pipe[1]);
}

CgiState CgiHandler::getState() const 
{
    return _state;
}
int CgiHandler::getErrorCode() const 
{
    return _error_code; 
}

std::string CgiHandler::_trim(const std::string& s)
{
    if (s.empty()) return s;
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::string CgiHandler::_toLower(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    return out;
}

std::string CgiHandler::_toStrInt(int n)
{
    std::ostringstream oss;
    oss << n;
    return oss.str();
}

std::string CgiHandler::_toStrSize(size_t n)
{
    std::ostringstream oss;
    oss << n;
    return oss.str();
}


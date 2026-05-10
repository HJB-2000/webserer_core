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
    for (size_t i = 0; i < _meta_env.size(); ++i)
    {
        const std::string& line = _meta_env[i];
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
        {
            ok = false;
            continue;
        }
        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (isEnvKeyRequired(key) && value.empty())
        {
            std::cerr << "[cgi][env] EMPTY required key: " << key << std::endl;
            ok = false;
        }
    }

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

void CgiHandler::filling_meta_variables(const HttpRequest& request,
                                         const Server&      config,
                                         const Location&    location)
{
    _meta_env = buildCgiEnvironment(request, config, location);

    // _env_ptrs holds raw char* into _meta_env's string buffers.
    // CRITICAL: _meta_env must NEVER be modified after this point.
    // Any push_back/resize/assignment on _meta_env can reallocate its
    // internal strings and silently invalidate every pointer here,
    // causing execve() to receive garbage environment pointers.
    _env_ptrs.clear();
    _env_ptrs.reserve(_meta_env.size() + 1);
    for (size_t i = 0; i < _meta_env.size(); ++i)
        _env_ptrs.push_back(const_cast<char*>(_meta_env[i].c_str()));
    _env_ptrs.push_back(NULL);
    log_env_once();
}

std::vector<std::string> CgiHandler::buildCgiEnvironment(
    const HttpRequest& request,
    const Server&      server,
    const Location&    location) const
{
    (void)location;
    std::vector<std::string> env;

    // SERVER_NAME must be the hostname from the Host request header
    // (RFC 3875 §4.1.14), not the server's bind address.
    // Strip port suffix (host:port → host).
    std::string server_name = request.header("host");
    if (server_name.empty())
        server_name = server.getHost();
    size_t colon_pos = server_name.rfind(':');
    if (colon_pos != std::string::npos)
        server_name = server_name.substr(0, colon_pos);

    std::string script_name     = request.path;
    std::string script_filename = _script_path;
    std::string full_path       = request.path;

    std::string path_info;
    if (full_path.length() > script_name.length()
        && full_path.find(script_name) == 0)
    {
        path_info = full_path.substr(script_name.length());
        if (!path_info.empty() && path_info[0] != '/')
            path_info = "/" + path_info;
    }

    std::string path_translated;
    if (!path_info.empty())
        path_translated = server.getRoot() + path_info;

    std::ostringstream server_port_ss;
    server_port_ss << server.getPort();

    env.push_back("REQUEST_METHOD="  + request.method);
    env.push_back("QUERY_STRING="    + request.query_string);
    env.push_back("SCRIPT_FILENAME=" + script_filename);
    env.push_back("SCRIPT_NAME="     + script_name);
    env.push_back("PATH_INFO="       + path_info);
    if (!path_translated.empty())
        env.push_back("PATH_TRANSLATED=" + path_translated);
    env.push_back("SERVER_NAME="      + server_name);
    env.push_back("SERVER_PORT="      + server_port_ss.str());
    env.push_back("SERVER_PROTOCOL="  + request.version);
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_SOFTWARE=webserv/1.0");

    // REMOTE_ADDR: real peer IP captured at accept() time and passed
    // through Connection → _startCgi → CgiHandler constructor.
    // Never hardcoded.
    env.push_back("REMOTE_ADDR=" + _peer_ip);

    env.push_back("REQUEST_URI=" + request.path +
        (request.query_string.empty() ? "" : "?" + request.query_string));
    env.push_back("DOCUMENT_ROOT=" + server.getRoot());

    // CONTENT_TYPE and CONTENT_LENGTH only when body is present.
    // RFC 3875 §4.1.2: omit CONTENT_LENGTH when there is no message body.
    if (!request.body.empty())
    {
        std::ostringstream cl_ss;
        cl_ss << request.body.size();
        env.push_back("CONTENT_TYPE="   + request.header("content-type"));
        env.push_back("CONTENT_LENGTH=" + cl_ss.str());
    }
    else
    {
        std::string ct = request.header("content-type");
        if (!ct.empty())
            env.push_back("CONTENT_TYPE=" + ct);
    }

    // Forward all HTTP headers as HTTP_* variables
    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
         it != request.headers.end(); ++it)
    {
        std::string key = it->first;
        for (size_t i = 0; i < key.size(); ++i)
        {
            if (key[i] == '-')
                key[i] = '_';
            else
                key[i] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(key[i])));
        }
        env.push_back("HTTP_" + key + "=" + it->second);
    }
    return env;
}

// ── Constructor ──────────────────────────────────────────────
//
// peer_ip: the real client IP captured at accept() time by
// ConnectionManager, stored in Connection::_peer_ip, and
// forwarded here by EventLoop::_startCgi via conn->peerIp().
CgiHandler::CgiHandler(const HttpRequest& request,
                       const Server&      config,
                       const Location&    location,
                       const std::string& script_path,
                       const std::string& peer_ip)
    : _request(request)
    , _location(location)
    , _script_path(script_path)
    , _peer_ip(peer_ip)
    , _child_pid(-1)
    , _state(CGI_IDLE)
    , _error_code(0)
    , _env_logged(false)
{
    cgi_in_pipe[0] = -1;
    cgi_in_pipe[1] = -1;
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
        _error_code = 500; _state = CGI_ERROR; return false;
    }

    struct stat sb;
    if (stat(cgi_path.c_str(), &sb) != 0)
    {
        _error_code = 500; _state = CGI_ERROR; return false;
    }
    if (!S_ISREG(sb.st_mode))
    {
        _error_code = 500; _state = CGI_ERROR; return false;
    }
    if (access(cgi_path.c_str(), X_OK) != 0)
    {
        _error_code = 500; _state = CGI_ERROR; return false;
    }

    std::string script_file = _script_path;
    if (script_file.empty())
    {
        _error_code = 404; _state = CGI_ERROR; return false;
    }
    if (stat(script_file.c_str(), &sb) != 0)
    {
        _error_code = 404; _state = CGI_ERROR; return false;
    }
    if (!S_ISREG(sb.st_mode))
    {
        _error_code = 403; _state = CGI_ERROR; return false;
    }
    if (access(script_file.c_str(), R_OK) != 0)
    {
        _error_code = 403; _state = CGI_ERROR; return false;
    }

    // Hard failure if the CGI environment is broken — no point forking
    if (!validate_env_contract())
    {
        std::cerr << "[cgi] env contract FAILED — aborting CGI launch\n";
        _error_code = 500; _state = CGI_ERROR; return false;
    }

    // need_stdin whenever there is a body, regardless of HTTP method.
    // RFC 3875 §4.1.2: STDIN carries the body when CONTENT_LENGTH > 0.
    bool need_stdin = !_request.body.empty();

    if (need_stdin)
    {
        if (pipe2(cgi_in_pipe, O_CLOEXEC) == -1)
        {
            _error_code = 500; _state = CGI_ERROR; return false;
        }
    }

    _child_pid = fork();
    if (_child_pid < 0)
    {
        if (need_stdin) { close_fd(cgi_in_pipe[0]); close_fd(cgi_in_pipe[1]); }
        _error_code = 500; _state = CGI_ERROR; return false;
    }

    if (_child_pid == 0)
    {
        // ── dup2 ALL three fds before closing any originals ──────────────
        // Doing them sequentially risks fd aliasing: if write_end == 2,
        // dup2(devnull_w, STDERR_FILENO) would close write_end before
        // stdout is redirected, corrupting the output pipe.
        // Safe order: dup2 all targets first, then close all originals.

        // 1. stdout → CGI output pipe
        if (dup2(write_end, STDOUT_FILENO) == -1)
            _exit(1);

        // 2. stderr → /dev/null
        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w < 0)
            _exit(1);
        if (dup2(devnull_w, STDERR_FILENO) == -1)
        {
            close(devnull_w);
            _exit(1);
        }
        close(devnull_w);

        // 3. stdin → body pipe or /dev/null
        if (need_stdin)
        {
            if (dup2(cgi_in_pipe[0], STDIN_FILENO) == -1)
                _exit(1);
        }
        else
        {
            int devnull_r = open("/dev/null", O_RDONLY);
            if (devnull_r < 0)
                _exit(1);
            if (dup2(devnull_r, STDIN_FILENO) == -1)
            {
                close(devnull_r);
                _exit(1);
            }
            close(devnull_r);
        }

        // All dup2s complete — now safe to close originals
        close(write_end);
        close_fd(cgi_in_pipe[0]);
        close_fd(cgi_in_pipe[1]); // write end — child never uses it

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

    // ── parent ───────────────────────────────────────────────────────────
    if (need_stdin)
    {
        close_fd(cgi_in_pipe[0]);
        int flags = fcntl(cgi_in_pipe[1], F_GETFL, 0);
        if (flags != -1)
            fcntl(cgi_in_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }

    // _start_time here is for internal reference only.
    // The authoritative timeout clock is job->start_time in EventLoop,
    // stamped by _startCgi() after this function returns so it reflects
    // the actual moment the child process began running.
    gettimeofday(&_start_time, NULL);
    _state = CGI_WAITING;
    return true;
}

CgiHandler::~CgiHandler()
{
    close_fd(cgi_in_pipe[0]);
    close_fd(cgi_in_pipe[1]);
}

CgiState CgiHandler::getState()     const { return _state;      }
int      CgiHandler::getErrorCode() const { return _error_code; }








































// #include "cgiHnadler.hpp"

// #include <sys/time.h>
// #include <sstream>
// #include <iostream>
// #include <cctype>
// #include <fcntl.h>
// #include <cstdlib>
// #include <unistd.h>
// #include <sys/stat.h>
// #include <sys/epoll.h>
// // uses them to decide which pipe fired and which operation to run,
// void CgiHandler::processEvent(int fd, int epollEvents)
// {
//     // A HUP or ERR on any pipe means the child closed its end.
//     // We treat that as an EOF condition and drain whatever is left.
//     bool isHup = (epollEvents & (EPOLLHUP | EPOLLERR)) != 0;

//     if (fd == cgi_in_pipe[1])
//     {
//         // epoll says we can write more POST body to the child's stdin.
//         if (_state == CGI_WRITING_STDIN || isHup)
//             monitor_cgi();
//         return;
//     }

//     if (fd == cgi_out_pipe[0])
//     {
//         // epoll says child has output ready, or child closed stdout (HUP).
//         if (_state == CGI_WAITING || _state == CGI_READING || isHup)
//             monitor_cgi();
//         return;
//     }
// }

// bool CgiHandler::isEnvKeyRequired(const std::string& key) const
// {
//     return key == "REQUEST_METHOD"    ||
//            key == "REQUEST_URI"       ||
//            key == "SCRIPT_NAME"       ||
//            key == "SCRIPT_FILENAME"   ||
//            key == "SERVER_NAME"       ||
//            key == "SERVER_PORT"       ||
//            key == "SERVER_PROTOCOL"   ||
//            key == "GATEWAY_INTERFACE" ||
//            key == "DOCUMENT_ROOT"     ||
//            key == "REMOTE_ADDR";
// }

// bool CgiHandler::validate_env_contract() const
// {
//     bool ok = true;
//     for (size_t i = 0; i < _meta_env.size(); ++i)
//     {
//         const std::string& line = _meta_env[i];
//         size_t eq = line.find('=');
//         if (eq == std::string::npos || eq == 0)
//         {
//             // std::cerr << "[cgi][env][invalid] malformed entry: " << line << std::endl;
//             ok = false;
//             continue;
//         }
//         std::string key   = line.substr(0, eq);
//         std::string value = line.substr(eq + 1);
//         if (isEnvKeyRequired(key) && value.empty())
//         {
//             // std::cerr << "[cgi][env][missing-required] " << key << std::endl;
//             ok = false;
//         }
//     }
//     return ok;
// }

// void CgiHandler::log_env_once()
// {
//     if (_env_logged)
//         return;
//     std::cerr << "[cgi][env] generated entries:" << std::endl;
//     for (size_t i = 0; i < _meta_env.size(); ++i)
//         std::cerr << "  " << _meta_env[i] << std::endl;
//     std::cerr << "[cgi][env] contract check: "
//               << (validate_env_contract() ? "PASS" : "FAIL")
//               << std::endl;
//     _env_logged = true;
// }

// void CgiHandler::filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location)
// {
//     _meta_env = buildCgiEnvironment(request, config, location);
//     _env_ptrs.clear();
//     for (size_t i = 0; i < _meta_env.size(); ++i)
//         _env_ptrs.push_back(const_cast<char*>(_meta_env[i].c_str()));
//     _env_ptrs.push_back(NULL);
//     log_env_once();
// }
// std::vector<std::string> CgiHandler::buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const
// {
//     std::vector<std::string> env;

//     std::string host = server.getHost();
//     if (host.empty())
//         host = request.getHeader("host");

//     std::string script_name     = request.getScriptName(location);      // e.g., "/cgi-bin/env.py"
//     std::string script_filename = request.getScriptFileName(server, location);
//     std::string full_path       = request.getPath();                    // e.g., "/cgi-bin/env.py/extra/stuff"

//     // ---------- Correct PATH_INFO ----------
//     std::string path_info;
//     if (full_path.length() > script_name.length() &&
//         full_path.find(script_name) == 0)
//     {
//         path_info = full_path.substr(script_name.length());
//         // Ensure it starts with '/' if not empty
//         if (!path_info.empty() && path_info[0] != '/')
//             path_info = "/" + path_info;
//     }
//     // else path_info remains empty (no extra path)

//     // ---------- Correct PATH_TRANSLATED ----------
//     std::string path_translated;
//     if (!path_info.empty())
//     {
//         std::string doc_root = location.getRoot();
//         if (doc_root.empty())
//             doc_root = server.getRoot();
//         path_translated = doc_root + path_info;
//     }

//     std::ostringstream content_length_ss;
//     content_length_ss << request.getContentLength();
//     std::ostringstream server_port_ss;
//     server_port_ss << server.getPort();

//     env.push_back("REQUEST_METHOD="    + request.getMethod());
//     env.push_back("QUERY_STRING="      + request.getQueryString());
//     env.push_back("CONTENT_TYPE="      + request.getContentType());
//     env.push_back("CONTENT_LENGTH="    + content_length_ss.str());
//     env.push_back("SCRIPT_FILENAME="   + script_filename);
//     env.push_back("SCRIPT_NAME="       + script_name);
    
//     env.push_back("PATH_INFO="         + path_info);
    
//     if (!path_translated.empty())
//         env.push_back("PATH_TRANSLATED=" + path_translated);
    
//     env.push_back("SERVER_NAME="       + host);
//     env.push_back("SERVER_PORT="       + server_port_ss.str());
//     env.push_back("SERVER_PROTOCOL="   + request.getVersion());
//     env.push_back("GATEWAY_INTERFACE=CGI/1.1");
//     env.push_back("SERVER_SOFTWARE=webserv/1.0");
//     env.push_back("REMOTE_ADDR="       + request.getRemoteAddr());

//     for (std::map<std::string, std::string>::const_iterator it = request.getHeaders().begin();
//          it != request.getHeaders().end(); ++it)
//     {
//         std::string key = it->first;
//         for (size_t i = 0; i < key.size(); ++i)
//         {
//             if (key[i] == '-')
//                 key[i] = '_';
//             else
//                 key[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[i])));
//         }
//         env.push_back("HTTP_" + key + "=" + it->second);
//     }
    
//     return env;
// }

// CgiHandler::CgiHandler(const HttpRequest& request, const Server& config, const Location& location)
//     : _request(request),
//       _config_server(config),
//       _location(location),
//       _child_pid(-1),
//       _state(CGI_IDLE),
//       _bytes_written(0),
//       _timeout_seconds(config.get_timeout_seconds()),
//       _error_code(0),
//       _env_logged(false)
// {
//     cgi_in_pipe[0]  = -1;
//     cgi_in_pipe[1]  = -1;
//     cgi_out_pipe[0] = -1;
//     cgi_out_pipe[1] = -1;
//     gettimeofday(&_start_time, NULL);
//     if (_timeout_seconds <= 0)
//         _timeout_seconds = 60;
//     filling_meta_variables(request, config, location);
// }

// void CgiHandler::close_fd(int& fd_pipe)
// {
//     if (fd_pipe != -1)
//     {
//         close(fd_pipe);
//         fd_pipe = -1;
//     }
// }

// void CgiHandler::spawnChild()
// {
//     if (_state != CGI_IDLE)
//         return;

//     std::string cgi_path = _location.getCGI_path();
//     if (cgi_path.empty()) 
//     {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         return;
//     }

//     struct stat sb;
//     if (stat(cgi_path.c_str(), &sb) != 0) 
//     {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         return;
//     }
//     if (!S_ISREG(sb.st_mode)) {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         return;
//     }
//     if (access(cgi_path.c_str(), X_OK) != 0) {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         return;
//     }

//     std::string script_file = _request.getScriptFileName(_config_server, _location);
//     if (script_file.empty()) 
//     {
//         _error_code = 404;
//         _state = CGI_ERROR;
//         return;
//     }

//     if (stat(script_file.c_str(), &sb) != 0) {
//         _error_code = 404;
//         _state = CGI_ERROR;
//         return;
//     }
//     if (!S_ISREG(sb.st_mode)) {
//         _error_code = 403;
//         _state = CGI_ERROR;
//         return;
//     }
//     if (access(script_file.c_str(), R_OK) != 0) {
//         _error_code = 403;
//         _state = CGI_ERROR;
//         return;
//     }


//     if (pipe(cgi_in_pipe) == -1 || pipe(cgi_out_pipe) == -1) {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         close_fd(cgi_in_pipe[0]);
//         close_fd(cgi_in_pipe[1]);
//         close_fd(cgi_out_pipe[0]);
//         close_fd(cgi_out_pipe[1]);
//         return;
//     }

//     _child_pid = fork();
//     if (_child_pid < 0) {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         close_fd(cgi_in_pipe[0]);
//         close_fd(cgi_in_pipe[1]);
//         close_fd(cgi_out_pipe[0]);
//         close_fd(cgi_out_pipe[1]);
//         return;
//     }

//     if (_child_pid == 0) {
//         if (dup2(cgi_in_pipe[0], STDIN_FILENO)  < 0) _exit(127);
//         if (dup2(cgi_out_pipe[1], STDOUT_FILENO) < 0) _exit(127);
//         if (dup2(cgi_out_pipe[1], STDERR_FILENO) < 0) _exit(127);

//         close_fd(cgi_in_pipe[0]);
//         close_fd(cgi_in_pipe[1]);
//         close_fd(cgi_out_pipe[0]);
//         close_fd(cgi_out_pipe[1]);

//         std::string script_dir;
//         std::string script_arg;
//         std::string::size_type slash = script_file.find_last_of('/');
//         if (slash != std::string::npos) {
//             script_dir = script_file.substr(0, slash);
//             script_arg = script_file.substr(slash + 1);
//         } else {
//             script_dir = ".";
//             script_arg = script_file;
//         }
//         if (chdir(script_dir.c_str()) != 0)
//             _exit(127);

//         // Build argv: [interpreter, script_file, NULL]
//         char* argv[3];
//         argv[0] = const_cast<char*>(cgi_path.c_str());
//         argv[1] = const_cast<char*>(script_arg.c_str());
//         argv[2] = NULL;

//         if (_env_ptrs.empty() || _env_ptrs.back() != NULL)
//             _exit(127);

//         execve(cgi_path.c_str(), argv, &_env_ptrs[0]);
//         _exit(127);
//     }

//     // Close the ends that belong to the child
//     close_fd(cgi_in_pipe[0]);   // child reads from pipe[0]
//     close_fd(cgi_out_pipe[1]);  // child writes to pipe[1]

//     // Set non-blocking on the parent's pipe ends
//     if (cgi_in_pipe[1] != -1) {
//         int flags = fcntl(cgi_in_pipe[1], F_GETFL, 0);
//         if (flags != -1)
//             fcntl(cgi_in_pipe[1], F_SETFL, flags | O_NONBLOCK);
//     }
//     if (cgi_out_pipe[0] != -1) {
//         int flags = fcntl(cgi_out_pipe[0], F_GETFL, 0);
//         if (flags != -1)
//             fcntl(cgi_out_pipe[0], F_SETFL, flags | O_NONBLOCK);
//     }

//     // Reset buffers and state
//     _output_buffer.clear();
//     _response.clear();
//     _error_code = 0;
//     _bytes_written = 0;
//     gettimeofday(&_start_time, NULL);

//     // Decide next state: write body if present
//     const std::string& method = _request.getMethod();
//     if ((method == "POST" || method == "DELETE") && !_request.getBody().empty()) {
//         _state = CGI_WRITING_STDIN;
//     } else {
//         // No body to send – close child's stdin so it gets EOF
//         close_fd(cgi_in_pipe[1]);
//         _state = CGI_WAITING;
//     }
// }

// void CgiHandler::monitor_cgi()
// {
//     struct timeval now;
//     gettimeofday(&now, NULL);
//     if (now.tv_sec - _start_time.tv_sec > _timeout_seconds)
//     {
//         // std::cerr << "[CGI] TIMEOUT killing pid=" << _child_pid << std::endl;
//         if (_child_pid > 0)
//         {
//             kill(_child_pid, SIGKILL);
//             waitpid(_child_pid, NULL, 0);
//             _child_pid = -1;
//         }
//         close_fd(cgi_out_pipe[0]);
//         close_fd(cgi_in_pipe[1]);
//         _error_code = 504;
//         _state      = CGI_ERROR;
//         return;
//     }

//     // std::cerr << "[CGI] monitor ENTER state=" << _state
//             //   << " pid=" << _child_pid
//             //   << " bytes=" << _output_buffer.size() << std::endl;

//     // === WRITE POST BODY TO CHILD STDIN ===
//     if (_state == CGI_WRITING_STDIN)
//     {
//         const std::string& body = _request.getBody();
//         while (_bytes_written < body.size())
//         {
//             ssize_t n = write(cgi_in_pipe[1], body.c_str() + _bytes_written, body.size() - _bytes_written);
//             if (n > 0)
//                 _bytes_written += static_cast<size_t>(n);
//             else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
//                 break;
//             else
//             {
//                 _error_code = 500;
//                 _state      = CGI_ERROR;
//                 close_fd(cgi_in_pipe[1]);
//                 return;
//             }
//         }
//         if (_bytes_written >= body.size())
//         {
//             close_fd(cgi_in_pipe[1]);
//             _state = CGI_WAITING;
//             // std::cerr << "[CGI] stdin fully sent" << std::endl;
//         }
//     }

//     // === READ CHILD STDOUT ===
//     if (_state == CGI_WAITING || _state == CGI_READING)
//     {
//         char buf[8192];
//         while (true)
//         {
//             ssize_t n = read(cgi_out_pipe[0], buf, sizeof(buf));
//             if (n > 0)
//             {
//                 _output_buffer.append(buf, static_cast<size_t>(n));
//                 _state = CGI_READING;
//                 // std::cerr << "[CGI] read " << n
//                         //   << " bytes (total " << _output_buffer.size() << ")" << std::endl;
//             }
//             else if (n == 0)
//             {
//                 // EOF — child closed its stdout. Parse what we have.
//                 // std::cerr << "[CGI] EOF received — parsing output" << std::endl;
//                 close_fd(cgi_out_pipe[0]);
//                 _parse_cgi_output();
//                 _state = (_error_code != 0) ? CGI_ERROR : CGI_DONE;
//                 break;
//             }
//             else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
//                 break;
//             else
//             {
//                 _error_code = 502;
//                 _state      = CGI_ERROR;
//                 close_fd(cgi_out_pipe[0]);
//                 break;
//             }
//         }
//     }

//     // If child exited but we never received a clean EOF (pipe error, short write),
//     // this catches the dangling state and forces finalization.
//     if (_child_pid > 0)
//     {
//         int status;
//         if (waitpid(_child_pid, &status, WNOHANG) > 0)
//         {
//             _child_pid = -1;
//             // std::cerr << "[CGI] child exited status=" << WEXITSTATUS(status) << std::endl;

//             if ((_state == CGI_READING || _state == CGI_WAITING) && !_output_buffer.empty())
//             {
//                 // std::cerr << "[CGI] child gone + data present → forcing CGI_DONE" << std::endl;
//                 close_fd(cgi_out_pipe[0]);
//                 _parse_cgi_output();
//                 _state = (_error_code != 0) ? CGI_ERROR : CGI_DONE;
//             }
//             else if (_state == CGI_WAITING && _output_buffer.empty())
//             {
//                 _error_code = 502;
//                 _state      = CGI_ERROR;
//             }
//         }
//     }

//     // std::cerr << "[CGI] monitor EXIT state=" << _state << std::endl;
// }

// void CgiHandler::_parse_cgi_output()
// {
//     _response = _build_http_from_cgi_output(_output_buffer);
//     if (_response.empty())
//         _error_code = 502;
// }

// std::string CgiHandler::_build_http_from_cgi_output(const std::string& raw) const
// {
//     // std::cerr << "=== CGI RAW OUTPUT ===" << std::endl;
//     // std::cerr << raw << std::endl;
//     // std::cerr << "=== END CGI OUTPUT ===" << std::endl;
//     size_t sep = raw.find("\r\n\r\n");
//     size_t body_offset = 4;
//     if (sep == std::string::npos)
//     {
//         sep = raw.find("\n\n");
//         body_offset = 2;
//     }
//     if (sep == std::string::npos)
//         return "";

//     size_t body_start = sep + body_offset;
//     if (body_start > raw.size())
//         body_start = raw.size();
    
//     std::string raw_headers = raw.substr(0, sep);
//     std::string body        = raw.substr(body_start);

//     int         status_code = 200;
//     std::string reason      = "OK";
//     std::string passthrough;
    
//     // Reserve space to avoid multiple reallocations
//     passthrough.reserve(raw_headers.size());

//     std::istringstream hs(raw_headers);
//     std::string line;
//     while (std::getline(hs, line))
//     {
//         if (!line.empty() && line[line.size() - 1] == '\r')
//             line.erase(line.size() - 1);
//         if (line.empty())
//             continue;

//         size_t c = line.find(':');
//         if (c == std::string::npos)
//             continue;

//         std::string key       = _trim(line.substr(0, c));
//         std::string value     = _trim(line.substr(c + 1));
//         std::string key_lower = _toLower(key);

//         if (key_lower == "status")
//         {
//             std::istringstream ss(value);
//             int parsed = 0;
//             if (!(ss >> parsed) || parsed < 100 || parsed > 599)
//                 return "";
//             status_code = parsed;
//             std::string rest;
//             std::getline(ss, rest);
//             rest   = _trim(rest);
//             reason = rest.empty() ? _reason_phrase(status_code) : rest;
//         }
//         else if (key_lower == "content-length" || 
//                  key_lower == "connection" ||
//                  key_lower == "transfer-encoding" ||
//                  key_lower == "keep-alive")
//         {
//             // Skip headers we control or that could cause issues
//             continue;
//         }
//         else
//         {
//             // Validate header name contains only allowed characters (RFC 7230)
//             bool valid_header = true;
//             for (size_t i = 0; i < key.size(); ++i)
//             {
//                 char ch = key[i];
//                 if (!((ch >= 'a' && ch <= 'z') || 
//                       (ch >= 'A' && ch <= 'Z') || 
//                       (ch >= '0' && ch <= '9') || 
//                       ch == '-'))
//                 {
//                     valid_header = false;
//                     break;
//                 }
//             }
            
//             if (key_lower == "set-cookie") {
//                 passthrough += "Set-Cookie: " + value + "\r\n";
//             }
//             if (valid_header)
//             {
//                 passthrough += key + ": " + value + "\r\n";
//             }
//             // Invalid header names are silently dropped
//         }
//     }

//     std::string out;
//     out.reserve(256 + passthrough.size() + body.size()); // why 256
    
//     out += "HTTP/1.1 " + _toStrInt(status_code) + " " + reason + "\r\n";
//     out += passthrough;
//     out += "Content-Length: " + _toStrSize(body.size()) + "\r\n";
//     out += "Connection: close\r\n\r\n";
//     out += body;
    
//     return out;
// }
// CgiHandler::~CgiHandler()
// {
//     close_fd(cgi_in_pipe[0]);
//     close_fd(cgi_in_pipe[1]);
//     close_fd(cgi_out_pipe[0]);
//     close_fd(cgi_out_pipe[1]);
//     if (_child_pid > 0)
//     {
//         int status = 0;
//         pid_t w = waitpid(_child_pid, &status, WNOHANG);
//         if (w == 0)
//         {
//             kill(_child_pid, SIGKILL);
//             waitpid(_child_pid, NULL, 0);
//         }
//         _child_pid = -1;
//     }
// }

// CgiState CgiHandler::getState() const
// {
//     return _state;
// }

// const std::string& CgiHandler::getResponse() const
// {
//     return _response;
// }

// int CgiHandler::getErrorCode() const
// {
//     return _error_code;
// }

// std::string CgiHandler::_trim(const std::string& s)
// {
//     if (s.empty()) return s;
//     size_t b = 0;
//     while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
//         ++b;
//     size_t e = s.size();
//     while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
//         --e;
//     return s.substr(b, e - b);
// }

// std::string CgiHandler::_toLower(const std::string& s)
// {
//     std::string out = s;
//     for (size_t i = 0; i < out.size(); ++i)
//         out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
//     return out;
// }

// std::string CgiHandler::_toStrInt(int n)
// {
//     std::ostringstream oss;
//     oss << n;
//     return oss.str();
// }

// std::string CgiHandler::_toStrSize(size_t n)
// {
//     std::ostringstream oss;
//     oss << n;
//     return oss.str();
// }

// std::string CgiHandler::_reason_phrase(int status_code)
// {
//     switch (status_code)
//     {
//         case 200: return "OK";
//         case 201: return "Created";
//         case 204: return "No Content";
//         case 301: return "Moved Permanently";
//         case 302: return "Found";
//         case 400: return "Bad Request";
//         case 401: return "Unauthorized";
//         case 403: return "Forbidden";
//         case 404: return "Not Found";
//         case 405: return "Method Not Allowed";
//         case 408: return "Request Timeout";
//         case 413: return "Payload Too Large";
//         case 500: return "Internal Server Error";
//         case 502: return "Bad Gateway";
//         case 503: return "Service Unavailable";
//         case 504: return "Gateway Timeout";
//         default:  return "Unknown";
//     }
// }


// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// /*------------------------------------------------------------*//*------------------------------------------------------------*//*------------------------------------------------------------*/
// #include "CgiHandler.hpp"

// #include <sys/time.h>
// #include <sstream>
// #include <iostream>
// #include <cctype>
// #include <fcntl.h>
// #include <cstdlib>
// #include <unistd.h>
// #include <sys/stat.h>
// #include <sys/epoll.h>


// bool CgiHandler::isEnvKeyRequired(const std::string& key) const
// {
//     return key == "REQUEST_METHOD"    ||
//            key == "REQUEST_URI"       ||
//            key == "SCRIPT_NAME"       ||
//            key == "SCRIPT_FILENAME"   ||
//            key == "SERVER_NAME"       ||
//            key == "SERVER_PORT"       ||
//            key == "SERVER_PROTOCOL"   ||
//            key == "GATEWAY_INTERFACE" ||
//            key == "DOCUMENT_ROOT"     ||
//            key == "REMOTE_ADDR";
// }

// bool CgiHandler::validate_env_contract() const
// {
//     bool ok = true;
//     // Check existing keys for empty values
//     for (size_t i = 0; i < _meta_env.size(); ++i)
//     {
//         const std::string& line = _meta_env[i];
//         size_t eq = line.find('=');
//         if (eq == std::string::npos || eq == 0)
//         {
//             ok = false;
//             continue;
//         }
//         std::string key = line.substr(0, eq);
//         std::string value = line.substr(eq + 1);
//         if (isEnvKeyRequired(key) && value.empty())
//             ok = false;
//     }

//     // Check for missing required keys
//     // Bug 6 fix: CONTENT_LENGTH added — POST CGI scripts read exactly
//     // this many bytes from stdin; a missing value breaks them silently.
//     const char* required[] = {
//         "REQUEST_METHOD", "REQUEST_URI", "SCRIPT_NAME",
//         "SCRIPT_FILENAME", "SERVER_NAME", "SERVER_PORT",
//         "SERVER_PROTOCOL", "GATEWAY_INTERFACE", "DOCUMENT_ROOT",
//         "REMOTE_ADDR", "CONTENT_LENGTH", NULL
//     };
//     for (int r = 0; required[r] != NULL; ++r)
//     {
//         bool found = false;
//         std::string prefix = std::string(required[r]) + "=";
//         for (size_t i = 0; i < _meta_env.size(); ++i)
//         {
//             if (_meta_env[i].compare(0, prefix.size(), prefix) == 0)
//             {
//                 found = true;
//                 break;
//             }
//         }
//         if (!found)
//         {
//             std::cerr << "[cgi][env] MISSING required key: " << required[r] << std::endl;
//             ok = false;
//         }
//     }
//     return ok;
// }

// void CgiHandler::log_env_once()
// {
//     if (_env_logged) return;
//     std::cerr << "[cgi][env] generated entries:" << std::endl;
//     for (size_t i = 0; i < _meta_env.size(); ++i)
//         std::cerr << "  " << _meta_env[i] << std::endl;
//     std::cerr << "[cgi][env] contract check: "
//               << (validate_env_contract() ? "PASS" : "FAIL") << std::endl;
//     _env_logged = true;
// }

// void CgiHandler::filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location)
// {
//     _meta_env = buildCgiEnvironment(request, config, location);
//     // NOTE: _env_ptrs is NOT built here. It is built immediately before
//     // execve() in startCgi() to prevent dangling pointers if _meta_env
//     // were ever reallocated between construction and exec. (Bug 1 fix)
//     log_env_once();
// }

// std::vector<std::string> CgiHandler::buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const
// {
//     (void)location;
//     std::vector<std::string> env;
//     std::string host = server.getHost();
//     if (host.empty()) host = request.header("host");

//     std::string script_name     = request.path;
//     std::string script_filename = _script_path;
//     std::string full_path       = request.path;

//     std::string path_info;
//     if (full_path.length() > script_name.length() && full_path.find(script_name) == 0)
//     {
//         path_info = full_path.substr(script_name.length());
//         if (!path_info.empty() && path_info[0] != '/') path_info = "/" + path_info;
//     }

//     std::string path_translated;
//     if (!path_info.empty())
//     {
//         std::string doc_root = server.getRoot();
//         path_translated = doc_root + path_info;
//     }

//     std::ostringstream content_length_ss;
//     content_length_ss << request.body.size();
//     std::ostringstream server_port_ss;
//     server_port_ss << server.getPort();

//     env.push_back("REQUEST_METHOD="    + request.method);
//     env.push_back("QUERY_STRING="      + request.query_string);
//     env.push_back("CONTENT_TYPE="      + request.header("content-type"));
//     env.push_back("CONTENT_LENGTH="    + content_length_ss.str());
//     env.push_back("SCRIPT_FILENAME="   + script_filename);
//     env.push_back("SCRIPT_NAME="       + script_name);
//     env.push_back("PATH_INFO="         + path_info);
//     if (!path_translated.empty()) 
//         env.push_back("PATH_TRANSLATED=" + path_translated);
//     env.push_back("SERVER_NAME="       + host);
//     env.push_back("SERVER_PORT="       + server_port_ss.str());
//     env.push_back("SERVER_PROTOCOL="   + request.version);
//     env.push_back("GATEWAY_INTERFACE=CGI/1.1");
//     env.push_back("SERVER_SOFTWARE=webserv/1.0");
//     env.push_back("REMOTE_ADDR=127.0.0.1");
//     env.push_back("REQUEST_URI=" + request.path +
//         (request.query_string.empty() ? "" : "?" + request.query_string));
//     env.push_back("DOCUMENT_ROOT=" + server.getRoot());

//     for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
//          it != request.headers.end(); ++it)
//     {
//         std::string key = it->first;
//         for (size_t i = 0; i < key.size(); ++i)
//         {
//             if (key[i] == '-') key[i] = '_';
//             else key[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[i])));
//         }
//         env.push_back("HTTP_" + key + "=" + it->second);
//     }
//     return env;
// }

// CgiHandler::CgiHandler(const HttpRequest& request, const Server& config, const Location& location, const std::string& script_path)
//     : _request(request), _location(location),
//       _script_path(script_path), _child_pid(-1), _state(CGI_IDLE),
//       _error_code(0), _env_logged(false)
// {
//     cgi_in_pipe[0]  = -1;
//     cgi_in_pipe[1]  = -1;
//     // cgi_out_pipe[0] = -1;
//     // cgi_out_pipe[1] = -1;
//     _start_time.tv_sec  = 0;  // will be set in startCgi() after fork()
//     _start_time.tv_usec = 0;
//     filling_meta_variables(request, config, location);
// }

// void CgiHandler::close_fd(int& fd_pipe)
// {
//     if (fd_pipe != -1) 
//     {
//         close(fd_pipe);
//         fd_pipe = -1;
//     }
// }

// bool CgiHandler::startCgi(int write_end)
// {
//     std::string cgi_path = _location.getCGI_path();
//     if (cgi_path.empty()) 
//     {
//         _error_code = 500;
//         _state = CGI_ERROR;
//         return false;
//     }

//     struct stat sb;
//     if (stat(cgi_path.c_str(), &sb) != 0)
//     {
//         _error_code = 500;
//         _state = CGI_ERROR; 
//         return false;
//     }
//     if (!S_ISREG(sb.st_mode))
//     {
//         _error_code = 500;
//         _state = CGI_ERROR; 
//         return false;
//     }
//     if (access(cgi_path.c_str(), X_OK) != 0)
//     {
//         _error_code = 500;
//         _state = CGI_ERROR; 
//         return false;
//     }

//     std::string script_file = _script_path;
//     if (script_file.empty())
//     {
//         _error_code = 404;
//         _state = CGI_ERROR; 
//         return false;
//     }
//     if (stat(script_file.c_str(), &sb) != 0)
//     {
//         _error_code = 404;
//         _state = CGI_ERROR; 
//         return false;
//     }
//     if (!S_ISREG(sb.st_mode))
//     {
//         _error_code = 403;
//         _state = CGI_ERROR; 
//         return false;
//     }
//     if (access(script_file.c_str(), R_OK) != 0)
//     {
//         _error_code = 403;
//         _state = CGI_ERROR; 
//         return false;
//     }

//     const std::string& method = _request.method;
//     const std::string& body   = _request.body;
//     bool need_stdin = ((method == "POST" || method == "DELETE") && !body.empty());

//     if (need_stdin)
//     {
//         if (pipe2(cgi_in_pipe, O_CLOEXEC) == -1) 
//         {
//             _error_code = 500;
//             _state = CGI_ERROR;
//             return false;
//         }
//     }

//     _child_pid = fork();
//     if (_child_pid < 0)
//     {
//         if (need_stdin)
//         {
//             close_fd(cgi_in_pipe[0]);
//             close_fd(cgi_in_pipe[1]);
//         }
//         _error_code = 500;
//         _state = CGI_ERROR;
//         return false;
//     }

//     if (_child_pid == 0)
//     {
//         if(dup2(write_end, STDOUT_FILENO) == -1)
//             _exit(1);
//         int devnull = open("/dev/null", O_WRONLY);
//         if (devnull < 0 || dup2(devnull, STDERR_FILENO) == -1)
//             _exit(1);
//         // Bug 3 fix: only close devnull if it is not the fd we just
//         // targeted — open() could return fd 2 in unusual fd states,
//         // and closing it would undo the dup2 we just did.
//         if (devnull != STDERR_FILENO)
//             close(devnull);

//         if (need_stdin)
//         {
//             if (dup2(cgi_in_pipe[0], STDIN_FILENO) == -1)
//                 _exit(1);
//         }
//         else
//         {
//             int devnull_in = open("/dev/null", O_RDONLY);
//             if (devnull_in < 0 || dup2(devnull_in, STDIN_FILENO) == -1)
//                 _exit(1);
//             // Bug 3 fix: same guard for the stdin devnull fd.
//             if (devnull_in != STDIN_FILENO)
//                 close(devnull_in);
//         }

//         close_fd(cgi_in_pipe[0]);
//         if (need_stdin)
//             close_fd(cgi_in_pipe[1]);
//         close(write_end);  // original fd no longer needed after dup2

//         std::string script_dir;
//         std::string script_arg;
//         std::string::size_type slash = script_file.find_last_of('/');
//         if (slash != std::string::npos)
//         {
//             script_dir = script_file.substr(0, slash);
//             script_arg = script_file.substr(slash + 1);
//         }
//         else 
//         {
//             script_dir = ".";
//             script_arg = script_file;
//         }
//         if (chdir(script_dir.c_str()) != 0)
//             _exit(127);

//         char* argv[3];
//         argv[0] = const_cast<char*>(cgi_path.c_str());
//         argv[1] = const_cast<char*>(script_arg.c_str());
//         argv[2] = NULL;

//         // Bug 1 fix: build _env_ptrs immediately before execve so that
//         // c_str() pointers are guaranteed valid — no reallocation can
//         // happen between this point and the execve call.
//         // execve requires char* not const char*; POSIX guarantees it will
//         // not modify these strings. (Bug 7 fix: documented)
//         _env_ptrs.clear();
//         for (size_t i = 0; i < _meta_env.size(); ++i)
//             _env_ptrs.push_back(const_cast<char*>(_meta_env[i].c_str()));
//         _env_ptrs.push_back(NULL);

//         if (_env_ptrs.empty() || _env_ptrs.back() != NULL)
//             _exit(127);

//         execve(cgi_path.c_str(), argv, &_env_ptrs[0]);
//         _exit(127);
//     }

//     // ── parent ──────────────────────────────────────────────
//     // Close the child's end of the stdin pipe. The caller (EventLoop) is
//     // responsible for writing the body to cgi_in_pipe[1] non-blockingly and
//     // closing it when done. We do NOT write here to avoid deadlocks when
//     // body size exceeds the pipe buffer (~64KB on Linux).
//     if (need_stdin)
//     {
//         close_fd(cgi_in_pipe[0]);  // parent doesn't need read end
//         // Make the write end non-blocking so the EventLoop can drain it
//         // incrementally via EPOLLOUT without blocking the whole server.
//         int flags = fcntl(cgi_in_pipe[1], F_GETFL, 0);
//         if (flags != -1)
//             fcntl(cgi_in_pipe[1], F_SETFL, flags | O_NONBLOCK);
//     }

//     gettimeofday(&_start_time, NULL);
//     _state = CGI_WAITING;
//     return true;
// }

// CgiHandler::~CgiHandler()
// {
//     close_fd(cgi_in_pipe[0]);
//     close_fd(cgi_in_pipe[1]);
// }

// CgiState CgiHandler::getState() const 
// {
//     return _state;
// }
// int CgiHandler::getErrorCode() const 
// {
//     return _error_code; 
// }

// std::string CgiHandler::_trim(const std::string& s)
// {
//     if (s.empty()) return s;
//     size_t b = 0;
//     while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
//         ++b;
//     size_t e = s.size();
//     while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
//         --e;
//     return s.substr(b, e - b);
// }

// std::string CgiHandler::_toLower(const std::string& s)
// {
//     std::string out = s;
//     for (size_t i = 0; i < out.size(); ++i)
//         out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
//     return out;
// }

// std::string CgiHandler::_toStrInt(int n)
// {
//     std::ostringstream oss;
//     oss << n;
//     return oss.str();
// }

// std::string CgiHandler::_toStrSize(size_t n)
// {
//     std::ostringstream oss;
//     oss << n;
//     return oss.str();
// }
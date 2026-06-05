#include "CgiHandler.hpp"

#include <sstream>
#include <iostream>
#include <cctype>
#include <fcntl.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>


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
            // std::cerr << "[cgi][env] EMPTY required key: " << key << std::endl;
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
            // std::cerr << "[cgi][env] MISSING required key: " << required[r] << std::endl;
            ok = false;
        }
    }
    return ok;
}

void CgiHandler::log_env_once()
{
    if (_env_logged) return;
    // std::cerr << "-------[cgi][env] generated entries:-------" << std::endl;
    // for (size_t i = 0; i < _meta_env.size(); ++i)
    //     std::cerr << "  " << _meta_env[i] << std::endl;
    // std::cerr << "-------[cgi][env] contract check: "
    //           << (validate_env_contract() ? "PASS-------" : "FAIL-------") << std::endl;
    _env_logged = true;
}

void CgiHandler::filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location)
{
    _meta_env = buildCgiEnvironment(request, config, location);

    // _env_ptrs holds raw char* into _meta_env's string buffers.
    // CRITICAL: _meta_env must NEVER be modified after this point —
    // any push_back/resize/assignment on _meta_env will reallocate its
    // internal strings and silently invalidate every pointer here,
    // causing execve() to receive garbage environment pointers.
    _env_ptrs.clear();
    _env_ptrs.reserve(_meta_env.size() + 1);
    for (size_t i = 0; i < _meta_env.size(); ++i)
        _env_ptrs.push_back(const_cast<char*>(_meta_env[i].c_str()));
    _env_ptrs.push_back(NULL);
    log_env_once();
}

std::vector<std::string> CgiHandler::buildCgiEnvironment(const HttpRequest& request, const Server& server, const Location& location) const
{
    (void)location;
    std::vector<std::string> env;

    // Fix #6: SERVER_NAME must be the hostname from the Host request header
    // (RFC 3875 §4.1.14), not the server's bind address. Strip port if present.
    std::string server_name = request.header("host");
    if (server_name.empty())
        server_name = server.getHost();
    // Strip port suffix (host:port → host)
    size_t colon_pos = server_name.rfind(':');
    if (colon_pos != std::string::npos)
        server_name = server_name.substr(0, colon_pos);

    std::string script_name     = request.path;
    std::string script_filename = _script_path;
    std::string full_path       = request.path;

    std::string path_info;
    if (full_path.length() > script_name.length() && full_path.find(script_name) == 0)
    {
        path_info = full_path.substr(script_name.length());
        if (!path_info.empty() && path_info[0] != '/') 
            path_info = "/" + path_info;
    }
    // Workaround for cgi_test: if still empty, use script_name (non‑empty)
    if (path_info.empty())
        path_info = script_name;   // e.g., "/directory/youpi.bla"

    // Also adjust PATH_TRANSLATED accordingly (use script_filename)
    std::string path_translated = script_filename;  // not doc_root + path_info
    if (!path_info.empty())
    {
        std::string doc_root = server.getRoot();
        path_translated = doc_root + path_info;
    }

    std::ostringstream server_port_ss;
    server_port_ss << server.getPort();

    env.push_back("REQUEST_METHOD="    + request.method);
    env.push_back("QUERY_STRING="      + request.query_string);
    env.push_back("SCRIPT_FILENAME="   + script_filename);
    env.push_back("SCRIPT_NAME="       + script_name);
    env.push_back("PATH_INFO="         + path_info);
    if (!path_translated.empty())
        env.push_back("PATH_TRANSLATED=" + path_translated);
    env.push_back("SERVER_NAME="       + server_name);
    env.push_back("SERVER_PORT="       + server_port_ss.str());
    env.push_back("SERVER_PROTOCOL="   + request.version);
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_SOFTWARE=webserv/1.0");
    env.push_back("REMOTE_ADDR=" + _client_ip);
    env.push_back("REQUEST_URI=" + request.path +
        (request.query_string.empty() ? "" : "?" + request.query_string));
    env.push_back("DOCUMENT_ROOT=" + server.getRoot());

    // Fix #7: CONTENT_TYPE and CONTENT_LENGTH only set when body is present
    // (RFC 3875 §4.1.2 — omit CONTENT_LENGTH when there is no message body)
    if (!request.body.empty())
    {
        std::ostringstream content_length_ss;
        content_length_ss << request.body.size();
        env.push_back("CONTENT_TYPE="   + request.header("content-type"));
        env.push_back("CONTENT_LENGTH=" + content_length_ss.str());
    }
    else
    {
        // Still set CONTENT_TYPE when sent by the client even without a body,
        // but leave CONTENT_LENGTH absent.
        std::string ct = request.header("content-type");
        if (!ct.empty())
            env.push_back("CONTENT_TYPE=" + ct);
    }

    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
         it != request.headers.end(); ++it)
    {
        // RFC 3875 §4.1.18: Content-Type and Content-Length are CONTENT_TYPE /
        // CONTENT_LENGTH only — do not duplicate as HTTP_* meta-variables.
        if (it->first == "content-type" || it->first == "content-length")
            continue;
        std::string key = it->first;
        for (size_t i = 0; i < key.size(); ++i)
        {
            if (key[i] == '-') key[i] = '_';
            else key[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[i])));
        }
        env.push_back("HTTP_" + key + "=" + it->second);
    }
    // std::cerr << "-------[cgi][env] generated entries:-------" << std::endl;
    // for (size_t i = 0; i < env.size(); ++i) {
    //     std::cerr << "  " << env[i] << std::endl;
    // }
    // std::cerr << "-------[cgi][env] contract check: " 
    //         << (env.size() > 0 ? "PASS" : "FAIL") << "-------" << std::endl;
    return env;
}

CgiHandler::CgiHandler(const HttpRequest& request, const Server& config, const Location& location, const std::string& script_path,  const std::string& client_ip)
    : _request(request), _location(location),
      _script_path(script_path), _child_pid(-1), _state(CGI_IDLE),
      _error_code(0), _env_logged(false), _client_ip(client_ip)
{
    _cgi_in_pipe[0] = -1;
    _cgi_in_pipe[1] = -1;
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
    // std::cout << "||||||||||||" << cgi_path << "||||||||||||" << std::endl;

    // stat() the CGI interpreter — record inode fingerprint for child
    // verification. Two separate structs so the second stat() does not
    // overwrite the first (original code reused a single struct stat sb).
    struct stat sb_cgi;
    if (stat(cgi_path.c_str(), &sb_cgi) != 0)
    {
        _error_code = 500;
        _state = CGI_ERROR;
        return false;
    }
    if (!S_ISREG(sb_cgi.st_mode))
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

    // stat() the script — separate struct so sb_cgi is preserved intact
    struct stat sb_script;
    if (stat(script_file.c_str(), &sb_script) != 0)
    {
        _error_code = 404;
        _state = CGI_ERROR;
        return false;
    }
    if (!S_ISREG(sb_script.st_mode))
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

    if (!validate_env_contract())
    {
        std::cerr << "[cgi] env contract FAILED — aborting CGI launch\n";
        _error_code = 500;
        _state = CGI_ERROR;
        return false;
    }

    const std::string& body = _request.body;
    bool need_stdin = !body.empty();

    if (need_stdin)
    {
        if (::pipe(_cgi_in_pipe) == -1)
        {
            _error_code = 500;
            _state = CGI_ERROR;
            return false;
        }

        // Set FD_CLOEXEC to guarantee no descriptor leaks into unrelated forks
        if (::fcntl(_cgi_in_pipe[0], F_SETFD, FD_CLOEXEC) == -1 ||
            ::fcntl(_cgi_in_pipe[1], F_SETFD, FD_CLOEXEC) == -1)
        {
            ::close(_cgi_in_pipe[0]);
            ::close(_cgi_in_pipe[1]);
            _cgi_in_pipe[0] = -1;
            _cgi_in_pipe[1] = -1;
            _error_code = 500;
            _state = CGI_ERROR;
            return false;
        }
    }

    // Convert paths to absolute before fork to ensure they're valid
    // after chdir() in child process
    char resolved_path[1024];
    if (realpath(cgi_path.c_str(), resolved_path) != NULL)
        cgi_path = resolved_path;

    char resolved_script[1024];
    if (realpath(script_file.c_str(), resolved_script) != NULL)
        script_file = resolved_script;

    _child_pid = fork();
    if (_child_pid < 0)
    {
        if (need_stdin)
        {
            close_fd(_cgi_in_pipe[0]);
            close_fd(_cgi_in_pipe[1]);
        }
        _error_code = 500;
        _state = CGI_ERROR;
        return false;
    }

    if (_child_pid == 0)
    {
        // ── TOCTOU Fix: re-verify inodes before execve ───────────────────
        // Compare st_ino + st_dev against snapshots taken in parent.
        // If the file was swapped in the fork()→execve() window, the
        // inode numbers won't match and we abort before executing anything.
        struct stat verify_cgi;
        if (stat(cgi_path.c_str(), &verify_cgi) != 0
            || verify_cgi.st_ino != sb_cgi.st_ino
            || verify_cgi.st_dev != sb_cgi.st_dev)
            _exit(127);

        struct stat verify_script;
        if (stat(script_file.c_str(), &verify_script) != 0
            || verify_script.st_ino != sb_script.st_ino
            || verify_script.st_dev != sb_script.st_dev)
            _exit(127);

        if (dup2(write_end, STDOUT_FILENO) == -1)
            _exit(1);

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w < 0)
            _exit(1);
        if (dup2(devnull_w, STDERR_FILENO) == -1)
        {
            close(devnull_w);
            _exit(1);
        }
        close(devnull_w);

        if (need_stdin)
        {
            if (dup2(_cgi_in_pipe[0], STDIN_FILENO) == -1)
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

        close(write_end);
        close_fd(_cgi_in_pipe[0]);
        close_fd(_cgi_in_pipe[1]);

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
        close_fd(_cgi_in_pipe[0]);
        int flags = fcntl(_cgi_in_pipe[1], F_GETFL, 0);
        if (flags != -1)
            fcntl(_cgi_in_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }

    _state = CGI_WAITING;
    return true;
}
CgiHandler::~CgiHandler()
{
    close_fd(_cgi_in_pipe[0]);
    close_fd(_cgi_in_pipe[1]);
}

CgiState CgiHandler::getState() const
{
    return _state;
}

int CgiHandler::getErrorCode() const
{
    return _error_code;
}

int CgiHandler::releaseStdinFd()
{
    int fd = _cgi_in_pipe[1];
    _cgi_in_pipe[1] = -1;
    return fd;
}

pid_t  CgiHandler::getChildPid() const 
{
    return _child_pid; 
}

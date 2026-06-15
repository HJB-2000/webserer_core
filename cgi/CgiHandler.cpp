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
            ok = false;
    }
    return ok;
}

void CgiHandler::log_env_once()
{
    if (_env_logged) return;
    _env_logged = true;
}

void CgiHandler::filling_meta_variables(const HttpRequest& request, const Server& config, const Location& location)
{
    _meta_env = buildCgiEnvironment(request, config, location);
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
    if (full_path.length() > script_name.length() && full_path.find(script_name) == 0)
    {
        path_info = full_path.substr(script_name.length());
        if (!path_info.empty() && path_info[0] != '/') 
            path_info = "/" + path_info;
    }

    if (path_info.empty())
        path_info = script_name;

    std::string path_translated = script_filename;
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

    size_t payload_size = request.body_file_written;
    if (payload_size == 0)
        payload_size = request.written;
    if (payload_size == 0 && !request.body.empty())
        payload_size = request.body.size();

    if (payload_size > 0 || request.expectsBody())
    {
        std::ostringstream content_length_ss;
        content_length_ss << payload_size;
        env.push_back("CONTENT_TYPE="   + request.header("content-type"));
        env.push_back("CONTENT_LENGTH=" + content_length_ss.str());
    }
    else
    {
        std::string ct = request.header("content-type");
        if (!ct.empty())
            env.push_back("CONTENT_TYPE=" + ct);
    }

    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
         it != request.headers.end(); ++it)
    {
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
    return env;
}

CgiHandler::CgiHandler(const HttpRequest& request, const Server& config, const Location& location, const std::string& script_path,  const std::string& client_ip)
    : _request((HttpRequest &)request),_server(config), _location(location),
    
      _script_path(script_path), _child_pid(-1), _state(CGI_IDLE),
      _error_code(0), _env_logged(false), _client_ip(client_ip)
{
    _cgi_in_pipe[0] = -1;
    _cgi_in_pipe[1] = -1;
    _body_fd = -1;
    filling_meta_variables(request, config, location);
}

void CgiHandler::setBodyFd(int fd)
{
    _body_fd = fd;
}

void CgiHandler::close_fd(int& fd_pipe)
{
    if (fd_pipe != -1)
    {
        close(fd_pipe);
        fd_pipe = -1;
    }
}

static bool resolve_path(const std::string& path, 
                         const std::string& location_root,
                         const std::string& server_root,
                         std::string& resolved)
{
    struct stat sb;

    if (!path.empty() && path[0] == '/')
    {
        if (stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
        {
            resolved = path;
            return true;
        }
        return false;
    }

    if (path.size() >= 2 && path[0] == '.' && path[1] == '/')
    {
        if (stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
        {
            // std::cout <<  "|||" +  path + "||| "  << std::endl;  
            resolved = path;
            return true;
        }
        std::string rel = path.substr(2);
        if (!location_root.empty())
        {
            std::string candidate = location_root;
            if (candidate[candidate.size() - 1] != '/')
                candidate += '/';
            candidate += rel;
            if (stat(candidate.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
            {
                resolved = candidate;
                return true;
            }
        }
        if (!server_root.empty())
        {
            std::string candidate = server_root;
            if (candidate[candidate.size() - 1] != '/')
                candidate += '/';
            candidate += rel;
            if (stat(candidate.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
            {
                resolved = candidate;
                return true;
            }
        }
        return false;
    }

    return false;
}

bool CgiHandler::startCgi(int write_end)
{
    // std::string cgi_path = _location.getCGI_path();
    // if (cgi_path.empty()) { _error_code = 500; _state = CGI_ERROR; return false; }


    size_t dot_pos = _script_path.find_last_of(".");
    if (dot_pos == std::string::npos)
    {
        _error_code = 400;
        _state = CGI_ERROR;
        return false; 
    }
    std::string script_ext = _script_path.substr(dot_pos);
    const std::map<std::string, std::string>& cgi_map = _location.getCGI_map();
    std::map<std::string, std::string>::const_iterator it = cgi_map.find(script_ext);
    if (it == cgi_map.end())
    {
        _error_code = 501;
        _state = CGI_ERROR; 
        return false; 
    }
    std::string cgi_path = it->second;
    // std::cerr << "---cgi_path" << cgi_path << "!!!" << "\n";


    // Resolve cgi_path - try location root first, then server root
    std::string resolved_cgi;
    std::string loc_root = _location.getRoot();
    std::string srv_root = _server.getRoot();
    if (!resolve_path(cgi_path, loc_root, srv_root, resolved_cgi))
    { _request.error_code = 500; _state = CGI_ERROR; return false; }
    cgi_path = resolved_cgi;

    struct stat sb_cgi;
    if (stat(cgi_path.c_str(), &sb_cgi) != 0 || !S_ISREG(sb_cgi.st_mode) ||
        access(cgi_path.c_str(), X_OK) != 0)
    { _request.error_code = 500; _state = CGI_ERROR; return false; }

    std::string script_file = _script_path;
    if (script_file.empty()) { _request.error_code = 404; _state = CGI_ERROR; return false; }

    // Resolve script_file - try location root first, then server root
    std::string resolved_script;
    if (!resolve_path(script_file, loc_root, srv_root, resolved_script))
    { _request.error_code = 404; _state = CGI_ERROR; return false; }
    script_file = resolved_script;

    struct stat sb_script;
    if (stat(script_file.c_str(), &sb_script) != 0)
    { _request.error_code = 404; _state = CGI_ERROR; return false; }
    if (!S_ISREG(sb_script.st_mode) || access(script_file.c_str(), R_OK) != 0)
    { _request.error_code = 403; _state = CGI_ERROR; return false; }

    if (!validate_env_contract())
    { _request.error_code = 500; _state = CGI_ERROR; return false; }

    bool need_stdin = (_request.chunked || _request.content_length > 0
                       || _request.body_file_written > 0 || _body_fd >= 0);

    _child_pid = fork();
    if (_child_pid < 0) {
         _request.error_code = 500; _state = CGI_ERROR; return false; }

    if (_child_pid == 0)
    {
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

        if (dup2(write_end, STDOUT_FILENO) == -1) _exit(1);

        if (need_stdin && _body_fd >= 0)
        {
            struct stat sb;
            if (fstat(_body_fd, &sb) == 0) {
                // std::cerr << "[CGI-Child] TEMP FILE DEBUG:" << std::endl;
                // std::cerr << "[CGI-Child]   - fd: " << _body_fd << std::endl;
                // std::cerr << "[CGI-Child]   - file size: " << sb.st_size << " bytes" << std::endl;
                // std::cerr << "[CGI-Child]   - st_blocks: " << sb.st_blocks << " (512-byte blocks)" << std::endl;
                // std::cerr << "[CGI-Child]   - bytes on disk: " << (sb.st_blocks * 512) << " bytes" << std::endl;
            }
            // std::cerr << "[CGI-Child] Calling dup2(temp_fd=" << _body_fd << ", STDIN)" << std::endl;
            if (dup2(_body_fd, STDIN_FILENO) == -1)
                _exit(1);
            // std::cerr << "[CGI-Child] dup2 succeeded, STDIN now points to temp file fd" << std::endl;
            if (_body_fd != STDIN_FILENO) {
                ::close(_body_fd);
                // std::cerr << "[CGI-Child] Closed original temp fd " << _body_fd << std::endl;
            }
            // std::cerr << "[CGI-Child] About to execute CGI script..." << std::endl;
        }
        else if (need_stdin)
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

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w < 0) _exit(1);
        if (dup2(devnull_w, STDERR_FILENO) == -1) { close(devnull_w); _exit(1); }
        close(devnull_w);

        close(write_end);
        char* argv[3];
        argv[0] = const_cast<char*>(cgi_path.c_str());
        argv[1] = const_cast<char*>(script_file.c_str());
        argv[2] = NULL;
        if (_env_ptrs.empty() || _env_ptrs.back() != NULL) _exit(127);
        execve(cgi_path.c_str(), argv, &_env_ptrs[0]);
        _exit(127);
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

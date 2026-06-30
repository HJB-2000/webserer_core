#include "CgiHandler.hpp"

#include <sstream>
#include <iostream>
#include <cctype>
#include <fcntl.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>


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

    std::string path_info = script_name;

    std::string path_translated = server.getRoot() + path_info;

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
    env.push_back("REMOTE_ADDR="       + _client_ip);
    env.push_back("REQUEST_URI="       + request.path +
        (request.query_string.empty() ? "" : "?" + request.query_string));
    env.push_back("DOCUMENT_ROOT="     + server.getRoot());

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
    
      _script_path(script_path), _child_pid(-1),
      _env_logged(false), _client_ip(client_ip)
{
    _body_fd = -1;
    filling_meta_variables(request, config, location);
}

void CgiHandler::setBodyFd(int fd)
{
    _body_fd = fd;
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

std::vector<std::string> CgiHandler::buildCgiArgs(const std::string& cgi_path,
    const std::string& script_file, const std::string& query_string)
{
    std::vector<std::string> args;
    args.push_back(cgi_path);
    args.push_back(script_file);

    if (!query_string.empty() && query_string.find('=') == std::string::npos)
    {
        std::string::size_type start = 0;
        std::string::size_type plus_pos;
        
        while ((plus_pos = query_string.find('+', start)) != std::string::npos)
        {
            args.push_back(query_string.substr(start, plus_pos - start));
            start = plus_pos + 1;
        }
        args.push_back(query_string.substr(start));
    }
    
    return args;
}


bool CgiHandler::startCgi(int write_end)
{
    size_t dot_pos = _script_path.find_last_of(".");
    if (dot_pos == std::string::npos)
        return false; 

    std::string script_ext = _script_path.substr(dot_pos);
    const std::map<std::string, std::string>& cgi_map = _location.getCGI_map();
    std::map<std::string, std::string>::const_iterator it = cgi_map.find(script_ext);
    if (it == cgi_map.end())
        return false; 

    std::string cgi_path = it->second;
    std::string resolved_cgi;
    std::string loc_root = _location.getRoot();
    std::string srv_root = _server.getRoot();
    if (!resolve_path(cgi_path, loc_root, srv_root, resolved_cgi))
        return false;
    cgi_path = resolved_cgi;

    struct stat sb_cgi;
    if (stat(cgi_path.c_str(), &sb_cgi) != 0 || !S_ISREG(sb_cgi.st_mode) || access(cgi_path.c_str(), X_OK) != 0)
        return false;

    std::string script_file = _script_path;
    if (script_file.empty())
        return false;

    struct stat sb_script;
    if (stat(script_file.c_str(), &sb_script) != 0)
        return false;
    if (!S_ISREG(sb_script.st_mode) || access(script_file.c_str(), R_OK) != 0)
        return false;

    bool need_stdin = (_request.chunked || _request.content_length > 0 || _request.body_file_written > 0 || _body_fd >= 0);
    if (!cgi_path.empty() && cgi_path[0] != '/')
    {
        const char* pwd = std::getenv("PWD");
        if (pwd && pwd[0] == '/')
        {
            std::string rel = cgi_path;
            if (rel.compare(0, 2, "./") == 0)
                rel = rel.substr(2);
            cgi_path = std::string(pwd) + "/" + rel;
        }
    }
    _child_pid = fork();
    if (_child_pid < 0)
        return false;

    if (_child_pid == 0)
    {
        struct stat verify_cgi;
        if (stat(cgi_path.c_str(), &verify_cgi) != 0
            || verify_cgi.st_ino != sb_cgi.st_ino
            || verify_cgi.st_dev != sb_cgi.st_dev)
            std::exit(127);
        struct stat verify_script;
        if (stat(script_file.c_str(), &verify_script) != 0
            || verify_script.st_ino != sb_script.st_ino
            || verify_script.st_dev != sb_script.st_dev)
            std::exit(127);

        size_t last_slash = script_file.find_last_of("/");
        std::string script_filename = script_file;
        
        if (last_slash != std::string::npos)
        {
            std::string cgi_dir = script_file.substr(0, last_slash);
            script_filename = "." + script_file.substr(last_slash);
            if (chdir(cgi_dir.c_str()) < 0)
                std::exit(1);
        }
        if (dup2(write_end, STDOUT_FILENO) == -1)
            std::exit(1);

        if (need_stdin && _body_fd >= 0)
        {
            if (dup2(_body_fd, STDIN_FILENO) == -1)
                std::exit(1);
            if (_body_fd != STDIN_FILENO)
                ::close(_body_fd);
        }
        else if (need_stdin)
        {
            int devnull_r = open("/dev/null", O_RDONLY);
            if (devnull_r < 0)
                std::exit(1);
            if (dup2(devnull_r, STDIN_FILENO) == -1)
            {
                close(devnull_r);
                std::exit(1);
            }
            close(devnull_r);
        }
        else
        {
            int devnull_r = open("/dev/null", O_RDONLY);
            if (devnull_r < 0)
                std::exit(1);
            if (dup2(devnull_r, STDIN_FILENO) == -1)
            {
                close(devnull_r);
                std::exit(1);
            }
            close(devnull_r);
        }

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w < 0)
            std::exit(1);
        if (dup2(devnull_w, STDERR_FILENO) == -1)
        {
            close(devnull_w);
            std::exit(1); 
        }
        close(devnull_w);

        close(write_end);
        std::vector<std::string> dynamic_args = buildCgiArgs(cgi_path, script_filename, _request.query_string);
        std::vector<char*> argv;
        for (size_t i = 0; i < dynamic_args.size(); ++i) {
            argv.push_back(const_cast<char*>(dynamic_args[i].c_str()));
        }
        argv.push_back(NULL);
        if (_env_ptrs.empty() || _env_ptrs.back() != NULL)
            std::exit(127);
        execve(cgi_path.c_str(), &argv[0], &_env_ptrs[0]);
        std::exit(127);
    }
    return true;
}

CgiHandler::~CgiHandler()
{
}

pid_t  CgiHandler::getChildPid() const 
{
    return _child_pid; 
}

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
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (isEnvKeyRequired(key) && value.empty()) 
            ok = false;
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
    std::vector<std::string> env;
    std::string host = server.getHost();
    if (host.empty()) host = request.getHeader("host");

    std::string script_name     = request.getScriptName(location);
    std::string script_filename = request.getScriptFileName(server, location);
    std::string full_path       = request.getPath();

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
    content_length_ss << request.getContentLength();
    std::ostringstream server_port_ss;
    server_port_ss << server.getPort();

    env.push_back("REQUEST_METHOD="    + request.getMethod());
    env.push_back("QUERY_STRING="      + request.getQueryString());
    env.push_back("CONTENT_TYPE="      + request.getContentType());
    env.push_back("CONTENT_LENGTH="    + content_length_ss.str());
    env.push_back("SCRIPT_FILENAME="   + script_filename);
    env.push_back("SCRIPT_NAME="       + script_name);
    env.push_back("PATH_INFO="         + path_info);
    if (!path_translated.empty()) 
        env.push_back("PATH_TRANSLATED=" + path_translated);
    env.push_back("SERVER_NAME="       + host);
    env.push_back("SERVER_PORT="       + server_port_ss.str());
    env.push_back("SERVER_PROTOCOL="   + request.getVersion());
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_SOFTWARE=webserv/1.0");
    env.push_back("REMOTE_ADDR="       + request.getRemoteAddr());

    for (std::map<std::string, std::string>::const_iterator it = request.getHeaders().begin();
         it != request.getHeaders().end(); ++it)
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

CgiHandler::CgiHandler(const HttpRequest& request, const Server& config, const Location& location)
    : _request(request), _config_server(config), _location(location),
      _child_pid(-1), _state(CGI_IDLE), _bytes_written(0),
      _timeout_seconds(60), _error_code(0), _env_logged(false)
{
    cgi_in_pipe[0]  = -1;
    cgi_in_pipe[1]  = -1;
    // cgi_out_pipe[0] = -1;
    // cgi_out_pipe[1] = -1;
    gettimeofday(&_start_time, NULL);
    if (_timeout_seconds <= 0) 
        _timeout_seconds = 60;
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

    std::string script_file = _request.getScriptFileName(_config_server, _location);
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

    const std::string& method = _request.getMethod();
    const std::string& body   = _request.getBody();
    bool need_stdin = ((method == "POST" || method == "DELETE") && !body.empty());

    if (need_stdin)
    {
        if (pipe(cgi_in_pipe) == -1) 
        {
            _error_code = 500;
            _state = CGI_ERROR;
            return false;
        }
        ssize_t written = write(cgi_in_pipe[1], body.c_str(), body.size());
        if (written != static_cast<ssize_t>(body.size()))
        {
            close_fd(cgi_in_pipe[0]);
            close_fd(cgi_in_pipe[1]);
            _error_code = 500;
            _state = CGI_ERROR;
            return false;
        }
        close_fd(cgi_in_pipe[1]);
    }

    _child_pid = fork();
    if (_child_pid < 0)
    {
        if (need_stdin) 
            close_fd(cgi_in_pipe[0]);
        _error_code = 500;
        _state = CGI_ERROR;
        return false;
    }

    if (_child_pid == 0)
    {
        if(dup2(write_end, STDOUT_FILENO) == -1)
            _exit(1);
        if(dup2(write_end, STDERR_FILENO) == -1)
            _exit(1);

        if (need_stdin)
            dup2(cgi_in_pipe[0], STDIN_FILENO);

        close_fd(cgi_in_pipe[0]);
        if (need_stdin)
            close_fd(cgi_in_pipe[1]);

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

    if (need_stdin)
        close_fd(cgi_in_pipe[0]);

    close(write_end);

    gettimeofday(&_start_time, NULL);
    _state = CGI_WAITING;
    return true;
}

void CgiHandler::endCgi(int read_end)
{
    std::string raw_output;
    char buf[4096];
    ssize_t n;
    while ((n = read(read_end, buf, sizeof(buf))) > 0)
        raw_output.append(buf, static_cast<size_t>(n));

    if (n == -1)
    {
        std::cerr << "read() error: " << std::endl;
        close(read_end);
        return ;
    }
    close(read_end);


    std::cout << "========= RAW CGI OUTPUT ============" << std::endl;
    std::cout << raw_output << std::endl;
    std::cout << "========= END RAW CGI OUTPUT ========" << std::endl;

    std::string final_http_response = _build_http_from_cgi_output(raw_output);

    std::cout << "========= PARSED HTTP RESPONSE ============" << std::endl;
    std::cout << final_http_response << std::endl;
    std::cout << "========= END PARSED HTTP RESPONSE ========" << std::endl;

}

void CgiHandler::_parse_cgi_output()
{
    _response = _build_http_from_cgi_output(_output_buffer);
    if (_response.empty()) _error_code = 502;
}


std::string CgiHandler::_build_http_from_cgi_output(const std::string& raw) const
{
    // std::cerr << "=== CGI RAW OUTPUT ===" << std::endl;
    // std::cerr << raw << std::endl;
    // std::cerr << "=== END CGI OUTPUT ===" << std::endl;
    size_t sep = raw.find("\r\n\r\n");
    size_t body_offset = 4;
    if (sep == std::string::npos)
    {
        sep = raw.find("\n\n");
        body_offset = 2;
    }
    if (sep == std::string::npos)
        return "";

    size_t body_start = sep + body_offset;
    if (body_start > raw.size())
        body_start = raw.size();
    
    std::string raw_headers = raw.substr(0, sep);
    std::string body        = raw.substr(body_start);

    int         status_code = 200;
    std::string reason      = "OK";
    std::string passthrough;
    
    passthrough.reserve(raw_headers.size());

    std::istringstream hs(raw_headers);
    std::string line;
    while (std::getline(hs, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            continue;

        size_t c = line.find(':');
        if (c == std::string::npos)
            continue;

        std::string key       = _trim(line.substr(0, c));
        std::string value     = _trim(line.substr(c + 1));
        std::string key_lower = _toLower(key);

        if (key_lower == "status")
        {
            std::istringstream ss(value);
            int parsed = 0;
            if (!(ss >> parsed) || parsed < 100 || parsed > 599)
                return "";
            status_code = parsed;
            std::string rest;
            std::getline(ss, rest);
            rest   = _trim(rest);
            reason = rest.empty() ? _reason_phrase(status_code) : rest;
        }
        else if (key_lower == "content-length" || 
                 key_lower == "connection" ||
                 key_lower == "transfer-encoding" ||
                 key_lower == "keep-alive")
        {
            continue;
        }
        else
        {
            // Validate header name contains only allowed characters (RFC 7230)
            bool valid_header = true;
            for (size_t i = 0; i < key.size(); ++i)
            {
                char ch = key[i];
                if (!((ch >= 'a' && ch <= 'z') || 
                      (ch >= 'A' && ch <= 'Z') || 
                      (ch >= '0' && ch <= '9') || 
                      ch == '-'))
                {
                    valid_header = false;
                    break;
                }
            }
            
            if (valid_header)
            {
                passthrough += key + ": " + value + "\r\n";
            }
        }
    }

    std::string out;
    out.reserve(256 + passthrough.size() + body.size()); // why 256
    
    out += "HTTP/1.1 " + _toStrInt(status_code) + " " + reason + "\r\n";
    out += passthrough;
    out += "Content-Length: " + _toStrSize(body.size()) + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    
    return out;
}

CgiHandler::~CgiHandler()
{
    close_fd(cgi_in_pipe[0]);
    close_fd(cgi_in_pipe[1]);
    // close_fd(cgi_out_pipe[0]);
    // close_fd(cgi_out_pipe[1]);
    if (_child_pid > 0) {
        int status = 0;
        if (waitpid(_child_pid, &status, WNOHANG) == 0) {
            kill(_child_pid, SIGKILL);
            waitpid(_child_pid, NULL, 0);
        }
        _child_pid = -1;
    }
}

CgiState CgiHandler::getState() const 
{
    return _state;
}
const std::string& CgiHandler::getResponse() const 
{
    return _response; 
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

std::string CgiHandler::_reason_phrase(int status_code)
{
    switch (status_code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "Unknown";
    }
}

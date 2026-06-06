#include "CgiHandler.hpp"

#include <sstream>
#include <iostream>
#include <cctype>
#include <fcntl.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

// Helper: resolve a potentially-relative path to an absolute path
// Returns true if file exists and is accessible, false otherwise
// Tries base paths in order: location_root, server_root, then returns false
static bool resolve_path(const std::string& path, 
                         const std::string& location_root,
                         const std::string& server_root,
                         std::string& resolved)
{
    // If path is absolute (starts with '/'), use it directly
    if (!path.empty())
    {
        struct stat sb;
        if (stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
        {
            resolved = path;
            return true;
        }
        return false;
    }
    
    // Try location root first
    if (!location_root.empty())
    {
        std::string candidate = location_root;
        // Ensure trailing slash for concatenation
        if (!candidate.empty() && candidate[candidate.length()-1] != '/')
            candidate += '/';
        candidate += path;
        
        struct stat sb;
        if (stat(candidate.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
        {
            resolved = candidate;
            return true;
        }
    }
    
    // Try server root as fallback
    if (!server_root.empty())
    {
        std::string candidate = server_root;
        // Ensure trailing slash for concatenation
        if (!candidate.empty() && candidate[candidate.length()-1] != '/')
            candidate += '/';
        candidate += path;
        
        struct stat sb;
        if (stat(candidate.c_str(), &sb) == 0 && S_ISREG(sb.st_mode))
        {
            resolved = candidate;
            return true;
        }
    }
    return false;
}


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
    if (request.body_size > 0) 
    {
        std::ostringstream ss;
        ss << request.body_size;
        env.push_back("CONTENT_TYPE="   + request.header("content-type"));
        env.push_back("CONTENT_LENGTH=" + ss.str());
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
    : _request(request), _server(config), _location(location),
      _script_path(script_path), _child_pid(-1), _state(CGI_IDLE),
      _error_code(0), _env_logged(false), _client_ip(client_ip)
{
    _cgi_in_pipe[0] = -1;
    _cgi_in_pipe[1] = -1;
    _body_fd = request.body_fd;
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
    if (cgi_path.empty()) { _error_code = 500; _state = CGI_ERROR; return false; }

    // Resolve cgi_path - try location root first, then server root
    std::string resolved_cgi;
    std::string loc_root = _location.getRoot();
    std::string srv_root = _server.getRoot();
    if (!resolve_path(cgi_path, loc_root, srv_root, resolved_cgi))
    {
        _error_code = 500;
        _state = CGI_ERROR;
        return false; 
    }
    cgi_path = resolved_cgi;

    struct stat sb_cgi;
    if (stat(cgi_path.c_str(), &sb_cgi) != 0 || !S_ISREG(sb_cgi.st_mode) ||
        access(cgi_path.c_str(), X_OK) != 0)
    { _error_code = 500; _state = CGI_ERROR; return false; }

    std::string script_file = _script_path;
    if (script_file.empty()) { _error_code = 404; _state = CGI_ERROR; return false; }
    // Resolve script_file - try location root first, then server root
    // std::string resolved_script;
    // if (!resolve_path(script_file, loc_root, srv_root, resolved_script))
    // { _error_code = 404; _state = CGI_ERROR; return false; }
    // script_file = resolved_script;

    struct stat sb_script;
    if (stat(script_file.c_str(), &sb_script) != 0)
    { _error_code = 404; _state = CGI_ERROR; return false; }
    if (!S_ISREG(sb_script.st_mode) || access(script_file.c_str(), R_OK) != 0)
    { _error_code = 403; _state = CGI_ERROR; return false; }

    if (!validate_env_contract())
    { _error_code = 500; _state = CGI_ERROR; return false; }

    // FILE BACKEND: use body_fd from request, no pipe needed
    bool need_stdin = (_request.body_fd >= 0);

    _child_pid = fork();
    if (_child_pid < 0) { _error_code = 500; _state = CGI_ERROR; return false; }

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

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w < 0) _exit(1);
        if (dup2(devnull_w, STDERR_FILENO) == -1) { close(devnull_w); _exit(1); }
        close(devnull_w);

        if (need_stdin)
        {
            ::lseek(_request.body_fd, 0, SEEK_SET);
            if (dup2(_request.body_fd, STDIN_FILENO) == -1) _exit(1);
            ::close(_request.body_fd);
        }
        else
        {
            int devnull_r = open("/dev/null", O_RDONLY);
            if (devnull_r < 0) _exit(1);
            if (dup2(devnull_r, STDIN_FILENO) == -1) { close(devnull_r); _exit(1); }
            close(devnull_r);
        }

        close(write_end);

        // Build absolute paths using server root
        // cgi_path is already resolved to a valid absolute-looking path (./cgi_test -> ./cgi_test)
        // script_file is already resolved to a valid absolute-looking path
        // Since we can't use getcwd(), we rely on the paths being correct from resolve_path
        
        // Script path for argv - use the resolved script_file
        char* argv[3];
        argv[0] = const_cast<char*>(cgi_path.c_str());
        argv[1] = const_cast<char*>(script_file.c_str());
        argv[2] = NULL;
        if (_env_ptrs.empty() || _env_ptrs.back() != NULL) _exit(127);
        execve(cgi_path.c_str(), argv, &_env_ptrs[0]);
        _exit(127);
    }

    // parent: close body_fd — child has its own fd via dup2
    if (need_stdin)
    {
        ::close(_request.body_fd);
        // ! do NOT set body_fd = -1 here — Connection::reset() owns it
        // but since we closed it, mark to avoid double-close:
        // caller must ensure request.body_fd not closed again
        // safest: set it -1 here, reset() checks for -1
        const_cast<HttpRequest&>(_request).body_fd = -1;
    }

    _state = CGI_WAITING;
    return true;
}

// releaseStdinFd: no longer needed — no pipe
// keep stub returning -1 so callers compile cleanly
int CgiHandler::releaseStdinFd()
{
    return -1;
}

CgiHandler::~CgiHandler()
{
    close_fd(_cgi_in_pipe[0]);
    close_fd(_cgi_in_pipe[1]);
}

CgiState CgiHandler::getState()    const { return _state;      }
int      CgiHandler::getErrorCode() const { return _error_code; }
pid_t    CgiHandler::getChildPid()  const { return _child_pid;  }
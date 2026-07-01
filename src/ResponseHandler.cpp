#include "Headers/ResponseHandler.hpp"

#include <sys/stat.h>    
#include <sys/types.h>   
#include <dirent.h>      
#include <fcntl.h>       
#include <unistd.h>      
#include <ctime>         
#include <cerrno>        
#include <cstring>       
#include <sstream>       
#include <iostream>      
#include <cstdio>        

namespace {

struct MultipartFile {
    std::string filename;
    std::string content_type;
    const char* data;
    size_t      len;
};

const char* findSubstr(const char* begin, const char* end,
                        const std::string& needle)
{
    if (needle.empty()) return begin;
    size_t nlen = needle.size();
    if (static_cast<size_t>(end - begin) < nlen) return end;

    const char* limit = end - nlen + 1;
    for (const char* p = begin; p < limit; ++p)
    {
        size_t i = 0;
        while (i < nlen && p[i] == needle[i]) ++i;
        if (i == nlen) return p;
    }
    return end;
}

std::string trimToken(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

bool extractBoundary(const HttpRequest& req, std::string& boundary)
{
    std::string ct = req.header("content-type");
    if (ct.find("multipart/form-data") == std::string::npos)
        return false;

    size_t pos = ct.find("boundary=");
    if (pos == std::string::npos) return false;
    boundary = ct.substr(pos + 9);

    size_t semi = boundary.find(';');
    if (semi != std::string::npos) boundary = boundary.substr(0, semi);
    boundary = trimToken(boundary);
    if (boundary.size() >= 2 && boundary[0] == '"' && boundary[boundary.size()-1] == '"')
        boundary = boundary.substr(1, boundary.size() - 2);

    return !boundary.empty();
}

bool parseMultipartFirstFile(const char* body, size_t body_len,
                              const std::string& boundary,
                              MultipartFile& out)
{
    if (body == NULL || body_len == 0) return false;

    std::string delim = "--" + boundary;
    const char* begin = body;
    const char* end   = body + body_len;

    const char* part = findSubstr(begin, end, delim);
    if (part == end) return false;
    part += delim.size();

    if (part + 2 <= end && part[0] == '-' && part[1] == '-')
        return false;

    if (part + 2 <= end && part[0] == '\r' && part[1] == '\n')
        part += 2;

    const char* hdr_end = findSubstr(part, end, "\r\n\r\n");
    if (hdr_end == end) return false;

    std::string headers(part, static_cast<size_t>(hdr_end - part));

    size_t fn_pos = headers.find("filename=\"");
    if (fn_pos != std::string::npos) {
        fn_pos += 10;
        size_t fn_end = headers.find('"', fn_pos);
        if (fn_end != std::string::npos)
            out.filename = headers.substr(fn_pos, fn_end - fn_pos);
    }

    size_t ct_pos = headers.find("Content-Type:");
    if (ct_pos == std::string::npos)
        ct_pos = headers.find("content-type:");
    if (ct_pos != std::string::npos) {
        size_t v_start = headers.find(':', ct_pos) + 1;
        size_t v_end   = headers.find("\r\n", v_start);
        if (v_end == std::string::npos) v_end = headers.size();
        out.content_type = trimToken(headers.substr(v_start, v_end - v_start));
    }

    const char* data_start = hdr_end + 4;
    const char* next_delim = findSubstr(data_start, end, delim);
    if (next_delim == end) return false;

    const char* data_end = next_delim;
    if (data_end - data_start >= 2 &&
        *(data_end - 2) == '\r' && *(data_end - 1) == '\n')
        data_end -= 2;

    if (data_end < data_start) return false;

    out.data = data_start;
    out.len  = static_cast<size_t>(data_end - data_start);
    return true;
}

std::string sanitizeFilename(const std::string& raw)
{
    std::string name = raw;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    if (name.empty() || name == "." || name == "..")
        return "";
    return name;
}

}

static std::string normalizePath(const std::string& path)  
{  
    std::vector<std::string> segments;  
    std::string seg;  
    for (size_t i = 0; i < path.size(); ++i) {  
        if (path[i] == '/') {  
            if (!seg.empty()) {  
                if (seg == "..") {  
                    if (segments.empty())  
                        return "";
                    segments.pop_back();  
                } else if (seg != ".") {  
                    segments.push_back(seg);  
                }  
                seg.clear();  
            }  
        } else {  
            seg += path[i];  
        }  
    }  
    if (!seg.empty()) {  
        if (seg == "..") {  
            if (segments.empty())  
                return "";  
            segments.pop_back();  
        } else if (seg != ".") {  
            segments.push_back(seg);  
        }  
    }  

    std::string result = "/";  
    for (size_t i = 0; i < segments.size(); ++i) {  
        result += segments[i];  
        if (i + 1 < segments.size())  
            result += "/";  
    }  
    if (path.size() > 1 && path[path.size() - 1] == '/' && result[result.size() - 1] != '/')  
        result += "/";  
    return result;  
}

void ResponseHandler::handle(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    std::string safe_path = normalizePath(req.path);  
    if (safe_path.empty()) {  
        _sendErrorInternal(400, req, cfg, wb);  
        return;  
    }
    const_cast<HttpRequest&>(req).path = safe_path;

    const Location* loc = cfg.matchLocation(req.path);
    if (req.method == "HEAD")
    {
        _sendErrorInternal(405, req, cfg, wb);
        return;
    }
    std::string check_method = req.method;

    if (loc)
    {  
        if (!loc->getMethods().empty()  
            && !_methodAllowed(check_method, loc->getMethods())) 
        {  
            _sendErrorInternal(405, req, cfg, wb);  
            return;  
        }  
    }
    else {  
        if (check_method != "GET") {  
                _sendErrorInternal(405, req, cfg, wb);  
                return;  
        }  
    }

    if (loc && loc->getRedirectEnabled())
    {
        _sendRedirect(loc->getReturnRedirection_code(), loc->getReturnRedirection_path(), req, wb);
        return;
    }

    std::string fs_path = _resolveFsPath(req, loc, cfg);

    if (!req.path.empty() && req.path[req.path.size() - 1] == '/')
    {
        struct stat dir_st;
        if (::stat(fs_path.c_str(), &dir_st) < 0) {
            _sendErrorInternal(404, req, cfg, wb);
            return;
        }
        if (!S_ISDIR(dir_st.st_mode)) {
            _sendErrorInternal(404, req, cfg, wb);
            return;
        }
        if (req.method == "POST")
        {
            if (loc && !loc->getUploadStore().empty())
                _handlePost(req, *loc, cfg, wb);
            else
                _sendErrorInternal(405, req, cfg, wb);
            return;
        }

        if (req.method != "GET")
        {
            _sendErrorInternal(405, req, cfg, wb);
            return;
        }

        std::vector<std::string> idx_vec = (loc && !loc->getIndex_s().empty())
                                           ? loc->getIndex_s() : cfg.getIndex_s();
        std::string idx = idx_vec.empty() ? "index.html" : idx_vec[0];
        std::string idx_path = fs_path + idx;
        struct stat st;

        if (::stat(idx_path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        {
            _serveStaticFile(req, idx_path, cfg, wb);
            return;
        }

        bool autoindex = loc ? loc->getAutoindex() : false;
        if (autoindex)
        {
            if (::stat(fs_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                _sendDirectoryListing(req, fs_path, cfg, wb);
            else
                _sendErrorInternal(404, req, cfg, wb);
            return;
        }

        _sendErrorInternal(403, req, cfg, wb);
        return;
    }

    struct stat st;
    if (::stat(fs_path.c_str(), &st) < 0)
    {
        _sendErrorInternal(404, req, cfg, wb);
        return;
    }
    if (S_ISDIR(st.st_mode))
    {
        if (!req.path.empty() && req.path[req.path.size() - 1] != '/')
            const_cast<HttpRequest&>(req).path = req.path + "/";

        const Location* dir_loc = cfg.matchLocation(req.path);

        if (req.method == "POST")
        {
            if (dir_loc && !dir_loc->getUploadStore().empty())
                _handlePost(req, *dir_loc, cfg, wb);
            else
                _sendErrorInternal(405, req, cfg, wb);
            return;
        }

        if (req.method != "GET")
        {
            _sendErrorInternal(405, req, cfg, wb);
            return;
        }

        std::vector<std::string> idx_vec = (dir_loc && !dir_loc->getIndex_s().empty())
                                           ? dir_loc->getIndex_s() : cfg.getIndex_s();
        std::string idx = idx_vec.empty() ? "index.html" : idx_vec[0];

        std::string idx_path = fs_path + "/" + idx;
        struct stat dir_st;

        if (::stat(idx_path.c_str(), &dir_st) == 0 && S_ISREG(dir_st.st_mode))
        {
            _serveStaticFile(req, idx_path, cfg, wb);
            return;
        }

        bool autoindex = dir_loc ? dir_loc->getAutoindex() : false;
        if (autoindex)
        {
            std::string dir_path = fs_path;
            if (!dir_path.empty() && dir_path[dir_path.size() - 1] != '/')
                dir_path += '/';
            _sendDirectoryListing(req, dir_path, cfg, wb);
            return;
        }

        _sendErrorInternal(404, req, cfg, wb);
        return;
    }
    
    if (req.method == "GET")
    {
        _serveStaticFile(req, fs_path, cfg, wb);
    }
    else if (req.method == "POST")
    {
        if (loc && !loc->getUploadStore().empty())
            _handlePost(req, *loc, cfg, wb);
        else
            _sendErrorInternal(405, req, cfg, wb);
    }
    else if (req.method == "DELETE")
    {
        _handleDelete(fs_path, req, cfg, wb);
    }
    else
    {
        _sendErrorInternal(405, req, cfg, wb);
    }
}


void ResponseHandler::_serveStaticFile(
    const HttpRequest&  req,
    const std::string&  fs_path,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    int fd = ::open(fs_path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        if (errno == ENOENT)
            _sendErrorInternal(404, req, cfg, wb);
        else if (errno == EACCES || errno == EPERM)
            _sendErrorInternal(403, req, cfg, wb);
        else
            _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    struct stat st;
    if (::stat(fs_path.c_str(), &st) < 0)
    {
        ::close(fd);
        _sendErrorInternal(500, req, cfg, wb);
        return;
    }
    if (st.st_size > ALLOWEDSIZE)
    {
        ::close(fd);
        _sendErrorInternal(400, req, cfg, wb);
        return;
    }
    std::string ct        = _getMimeType(fs_path);
    size_t      file_size = static_cast<size_t>(st.st_size);

    _writeHeaders(200, ct, file_size, "", req, wb);


    if (req.method == "HEAD")
    {
        ::close(fd);
        return;
    }

    char    buf[8192];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0)
        _appendStr(wb, buf, static_cast<size_t>(n));

    ::close(fd);
}

void ResponseHandler::_sendDirectoryListing(
    const HttpRequest&  req,
    const std::string&  fs_path,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    DIR* dir = ::opendir(fs_path.c_str());
    if (!dir)
    {
        if (errno == ENOENT)
            _sendErrorInternal(404, req, cfg, wb);
        else if (errno == EACCES || errno == EPERM)
            _sendErrorInternal(403, req, cfg, wb);
        else
            _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html>\n"
         << "<head><meta charset=\"UTF-8\">"
         << "<title>Index of " << htmlEscape(req.path) << "</title>\n"
         << "<style>body{font-family:monospace;padding:1em}"
         << "table{border-collapse:collapse;width:100%}"
         << "th,td{text-align:left;padding:4px 12px}"
         << "tr:nth-child(even){background:#f2f2f2}"
         << "a{text-decoration:none}a:hover{text-decoration:underline}"
         << "</style></head>\n"
         << "<body>\n"
         << "<h1>Index of " << htmlEscape(req.path) << "</h1>\n"
         << "<hr>\n"
         << "<table>\n"
         << "<tr><th>Name</th><th>Size</th></tr>\n";

    if (req.path != "/")
        html << "<tr><td><a href=\"../\">../</a></td><td>-</td></tr>\n";

    struct dirent* ent;
    while ((ent = ::readdir(dir)) != NULL)
    {
        std::string name(ent->d_name);
        if (name == "." || name == "..")
            continue;

        std::string full = fs_path + name;
        struct stat st;
        bool   is_dir = false;
        size_t fsize  = 0;

        if (::stat(full.c_str(), &st) == 0)
        {
            is_dir = S_ISDIR(st.st_mode) != 0;
            if (!is_dir)
                fsize = static_cast<size_t>(st.st_size);
        }

        std::string href    = req.path + name;
        std::string display = name;
        if (is_dir) { href += "/"; display += "/"; }

        html << "<tr>"
             << "<td><a href=\"" << htmlEscape(href) << "\">" << htmlEscape(display) << "</a></td>"
             << "<td>";
        if (is_dir)
            html << "-";
        else
            html << fsize;
        html << "</td></tr>\n";
    }
    ::closedir(dir);

    html << "</table>\n<hr>\n</body>\n</html>\n";

    std::string body = html.str();
    _writeHeaders(200, "text/html", body.size(), "", req, wb);
    if (req.method != "HEAD")
        _appendStr(wb, body);
}

void ResponseHandler::_sendRedirect(
    int                code,
    const std::string& url,
    const HttpRequest& req,
    Buffer&            wb)
{
    std::ostringstream body_oss;
    body_oss << "<!DOCTYPE html><html><head><title>"
             << code << " " << _reasonPhrase(code)
             << "</title></head><body><p>Redirecting to "
             << "<a href=\"" << htmlEscape(url) << "\">" << htmlEscape(url) << "</a>"
             << "</p></body></html>";
    std::string body = body_oss.str();

    std::ostringstream extra;
    extra << "Location: " << url << "\r\n";

    _writeHeaders(code, "text/html", body.size(), extra.str(), req, wb);
    if (req.method != "HEAD")
        _appendStr(wb, body);
}

void ResponseHandler::_handlePost(
    const HttpRequest&  req,
    const Location&     loc,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    if (loc.getUploadStore().empty())
    {
        _sendErrorInternal(405, req, cfg, wb);
        return;
    }

    std::string upload_dir = loc.getUploadStore();
    if (upload_dir.empty() || upload_dir[upload_dir.size() - 1] != '/')
        upload_dir += '/';

    static int counter = 0;
    ++counter;
    time_t now = ::time(NULL);

    const char* write_data = req.body.data();
    size_t      write_len  = req.body.size();
    std::string filename;

    std::string boundary;
    MultipartFile mf;
    mf.data = NULL; mf.len = 0;

    if (extractBoundary(req, boundary) &&
        parseMultipartFirstFile(req.body.data(), req.body.size(), boundary, mf))
    {
        filename = sanitizeFilename(mf.filename);
        write_data = mf.data;
        write_len  = mf.len;
    }

    if (filename.empty())
    {
        std::ostringstream name_oss;
        name_oss << "upload_" << static_cast<long>(now)
                 << "_" << counter;
        filename = name_oss.str();
    }

    std::string filepath = upload_dir + filename;

    int fd = ::open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        std::cerr << "[ResponseHandler] POST upload open failed: "
                  << std::strerror(errno) << "\n";
        _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    size_t written = 0;
    while (written < write_len)
    {
        ssize_t n = ::write(fd, write_data + written, write_len - written);
        if (n <= 0)
        {
            ::close(fd);
            _sendErrorInternal(500, req, cfg, wb);
            return;
        }
        written += static_cast<size_t>(n);
    }
    ::close(fd);

    std::string location_url = req.path;
    if (location_url.empty() || location_url[location_url.size() - 1] != '/')
        location_url += '/';
    location_url += filename;
    std::string resp_body =
        "<!DOCTYPE html><html><body>"
        "<p>File uploaded successfully.</p>"
        "</body></html>";

    std::ostringstream extra;
    extra << "Location: " << location_url << "\r\n";

    _writeHeaders(201, "text/html", resp_body.size(), extra.str(), req, wb);
    if (req.method != "HEAD")
        _appendStr(wb, resp_body);
}

void ResponseHandler::_handleDelete(
    const std::string&  fs_path,
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{

    if (std::remove(fs_path.c_str()) != 0)
    {
        if (errno == ENOENT)
            _sendErrorInternal(404, req, cfg, wb);
        else if (errno == EACCES || errno == EPERM)
            _sendErrorInternal(403, req, cfg, wb);
        else
            _sendErrorInternal(500, req, cfg, wb);
        return;
    }


    std::ostringstream oss;
    oss << "HTTP/1.1 204 No Content\r\n"
        << "Server: webserv/1.1\r\n"
        << "Date: "       << _httpDate()             << "\r\n"
        << "Connection: " << _connectionHeader(req)  << "\r\n"
        << "\r\n";
    _appendStr(wb, oss.str());
}

void ResponseHandler::_writeHeaders(
    int                code,
    const std::string& ct,
    size_t             cl,
    const std::string& extra,
    const HttpRequest& req,
    Buffer&            wb)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << _reasonPhrase(code) << "\r\n"
        << "Server: webserv/1.1\r\n"
        << "Date: "           << _httpDate()            << "\r\n"
        << "Content-Type: "   << ct                     << "\r\n"
        << "Content-Length: " << cl                     << "\r\n";
    if (!extra.empty())
        oss << extra;
    oss << "Connection: "     << _connectionHeader(req) << "\r\n"
        << "\r\n";
    _appendStr(wb, oss.str());
}

std::string ResponseHandler::_resolveFsPath(
    const HttpRequest&  req,
    const Location*     loc,
    const ServerConfig& cfg) const
{
    std::string root = cfg.getRoot();
    std::string uri  = req.path;

    if (loc && !loc->getRoot().empty())
    {
        root = loc->getRoot();

        std::string loc_path = loc->getPath();
        if (uri.compare(0, loc_path.size(), loc_path) == 0)
        {
            std::string stripped = uri.substr(loc_path.size());
            if (!stripped.empty())
            {
                if (stripped[0] != '/')
                    stripped = loc_path + stripped;
                uri = stripped;
            }
        }
    }

    while (!root.empty() && root[root.size() - 1] == '/')
        root.erase(root.size() - 1);

    if (loc && !loc->getRoot().empty())
    {
        std::string loc_path = loc->getPath();
        if (uri == loc_path)
        {
            size_t last_slash = uri.rfind('/');
            if (last_slash != std::string::npos)
                uri = uri.substr(last_slash);
        }
    }

    return root + uri;
}

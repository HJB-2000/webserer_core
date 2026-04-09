// ============================================================
//  ResponseHandler.cpp  —  Phase 3 implementation
//
//  All methods declared in ResponseHandler.hpp live here.
//  C++98 compliant.
// ============================================================

#include "Headers/ResponseHandler.hpp"

// ── htmlEscape ───────────────────────────────────────────────
//
// Escapes the five characters that are special in HTML/XML.
// Applied to any user-controlled string inserted into HTML output
// (req.path in autoindex, Location URL in redirect body).
static std::string htmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += s[i];     break;
        }
    }
    return out;
}

#include <sys/stat.h>    // stat, fstat, S_ISREG, S_ISDIR
#include <sys/types.h>   // size_t, pid_t
#include <dirent.h>      // opendir, readdir, closedir
#include <fcntl.h>       // open, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC
#include <unistd.h>      // read, write, close, unlink, getpid
#include <ctime>         // time, gmtime, strftime
#include <cerrno>        // errno
#include <cstring>       // strerror
#include <sstream>       // std::ostringstream
#include <iostream>      // std::cerr


// ============================================================
//  Constructor  —  populate MIME table
// ============================================================
ResponseHandler::ResponseHandler()
{
    _mime[".html"]  = "text/html";
    _mime[".htm"]   = "text/html";
    _mime[".css"]   = "text/css";
    _mime[".js"]    = "application/javascript";
    _mime[".json"]  = "application/json";
    _mime[".xml"]   = "application/xml";
    _mime[".txt"]   = "text/plain";
    _mime[".pdf"]   = "application/pdf";
    _mime[".zip"]   = "application/zip";
    _mime[".tar"]   = "application/x-tar";
    _mime[".png"]   = "image/png";
    _mime[".jpg"]   = "image/jpeg";
    _mime[".jpeg"]  = "image/jpeg";
    _mime[".gif"]   = "image/gif";
    _mime[".ico"]   = "image/x-icon";
    _mime[".svg"]   = "image/svg+xml";
    _mime[".webp"]  = "image/webp";
    _mime[".mp4"]   = "video/mp4";
    _mime[".webm"]  = "video/webm";
    _mime[".mp3"]   = "audio/mpeg";
    _mime[".wav"]   = "audio/wav";
    _mime[".ogg"]   = "audio/ogg";
    _mime[".woff"]  = "font/woff";
    _mime[".woff2"] = "font/woff2";
    _mime[".ttf"]   = "font/ttf";
}


// ============================================================
//  PUBLIC: handle()
//
//  Decision tree (Plan.md §4):
//    1. Location match
//    2. Method allowed  → 405
//    3. Redirect        → 301/302
//    4. Resolve fs_path
//    5. Directory URI   → index / autoindex / 403
//    6. stat(fs_path)   → 404 / directory 301
//    7. CGI extension   → Phase 4 stub
//    8. GET/HEAD        → serveStaticFile
//       POST            → handlePost
//       DELETE          → handleDelete
//       other           → 405
// ============================================================
void ResponseHandler::handle(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    // ── Path traversal protection ─────────────────────────────
    // Reject any path containing a ".." component.
    const std::string& rp = req.path;
    if (rp.find("/../") != std::string::npos
        || (rp.size() >= 3 && rp.compare(rp.size() - 3, 3, "/..") == 0)
        || rp == "..")
    {
        _sendErrorInternal(400, req, cfg, wb);
        return;
    }

    // ── 1. Location match ─────────────────────────────────────
    const Location* loc = cfg.matchLocation(req.path);

    // ── 2. Method allowed ─────────────────────────────────────
    if (loc && !loc->getMethods().empty()
        && !_methodAllowed(req.method, loc->getMethods()))
    {
        _sendErrorInternal(405, req, cfg, wb);
        return;
    }

    // ── 3. Redirect ───────────────────────────────────────────
    if (loc && loc->getRedirectEnabled())
    {
        _sendRedirect(loc->getReturnRedirection_code(), loc->getReturnRedirection_path(), req, wb);
        return;
    }

    // ── 4. Resolve filesystem path ────────────────────────────
    std::string fs_path = _resolveFsPath(req, loc, cfg);

    // ── 5. Directory URI (path ends with '/') ─────────────────
    if (!req.path.empty() && req.path[req.path.size() - 1] == '/')
    {
        // POST upload to directory
        if (req.method == "POST")
        {
            if (loc && !loc->getUploadStore().empty())
                _handlePost(req, *loc, cfg, wb);
            else
                _sendErrorInternal(405, req, cfg, wb);
            return;
        }

        // Only GET/HEAD can serve directory content
        if (req.method != "GET" && req.method != "HEAD")
        {
            _sendErrorInternal(405, req, cfg, wb);
            return;
        }

        // Try index file
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

        // Autoindex
        bool autoindex = loc ? loc->getAutoindex() : false;
        if (autoindex)
        {
            if (::stat(fs_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                _sendDirectoryListing(req, fs_path, cfg, wb);
            else
                _sendErrorInternal(404, req, cfg, wb);
            return;
        }

        // No index, no autoindex → 403
        _sendErrorInternal(403, req, cfg, wb);
        return;
    }

    // ── 6. Check file exists ──────────────────────────────────
    struct stat st;
    if (::stat(fs_path.c_str(), &st) < 0)
    {
        _sendErrorInternal(404, req, cfg, wb);
        return;
    }

    // Directory without trailing slash → 301 to path + '/'
    if (S_ISDIR(st.st_mode))
    {
        _sendRedirect(301, req.path + "/", req, wb);
        return;
    }

    // ── 7. CGI check (Phase 4 seam) ───────────────────────────
    if (loc && !loc->getCGI_extension().empty())
    {
        const std::string ext = loc->getCGI_extension();
        if (req.path.size() >= ext.size()
            && req.path.compare(req.path.size() - ext.size(), ext.size(), ext) == 0)
        {
            _stubCgi(req, cfg, wb);
            return;
        }
    }

    // ── 8. Method dispatch ────────────────────────────────────
    if (req.method == "GET" || req.method == "HEAD")
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


// ============================================================
//  PUBLIC: sendError()
//
//  Called before a full request is available (parse errors,
//  body-limit violation).  Always sends Connection: close.
// ============================================================
void ResponseHandler::sendError(
    int                 code,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    std::string body = _loadErrorPage(code, cfg);

    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << _reasonPhrase(code) << "\r\n"
        << "Server: webserv/1.0\r\n"
        << "Date: "           << _httpDate()   << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size()   << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    _appendStr(wb, oss.str());
}


// ============================================================
//  _serveStaticFile
//
//  Opens the file, stats it, writes headers, then streams
//  the body (skipped for HEAD requests).
// ============================================================
void ResponseHandler::_serveStaticFile(
    const HttpRequest&  req,
    const std::string&  fs_path,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    int fd = ::open(fs_path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    struct stat st;
    if (::fstat(fd, &st) < 0)
    {
        ::close(fd);
        _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    std::string ct        = _getMimeType(fs_path);
    size_t      file_size = static_cast<size_t>(st.st_size);

    _writeHeaders(200, ct, file_size, "", req, wb);

    // HEAD → headers only, no body
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


// ============================================================
//  _sendDirectoryListing
//
//  Generates an HTML page listing directory contents.
//  Inspired by nginx's ngx_http_autoindex_module.
// ============================================================
void ResponseHandler::_sendDirectoryListing(
    const HttpRequest&  req,
    const std::string&  fs_path,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    DIR* dir = ::opendir(fs_path.c_str());
    if (!dir)
    {
        _sendErrorInternal(403, req, cfg, wb);
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

    // Parent directory link
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


// ============================================================
//  _sendRedirect
//
//  Builds a 301/302 response with a Location header.
//  Inspired by nginx's ngx_http_core_module redirect path.
// ============================================================
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


// ============================================================
//  _handlePost
//
//  Upload: saves request body to upload_path with a generated
//  filename.  Responds 201 Created with Location header.
//
//  CGI uploads are handled upstream (Phase 4 stub).
// ============================================================
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

    // Ensure upload directory path ends with '/'
    std::string upload_dir = loc.getUploadStore();
    if (upload_dir.empty() || upload_dir[upload_dir.size() - 1] != '/')
        upload_dir += '/';

    // Generate a unique filename
    static int counter = 0;
    ++counter;
    time_t now = ::time(NULL);
    std::ostringstream name_oss;
    name_oss << "upload_" << static_cast<long>(now)
             << "_" << static_cast<int>(::getpid())
             << "_" << counter;
    std::string filename = name_oss.str();
    std::string filepath = upload_dir + filename;

    // Write body to file
    int fd = ::open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        std::cerr << "[ResponseHandler] POST upload open failed: "
                  << std::strerror(errno) << "\n";
        _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    const std::string& body_data = req.body;
    size_t             written   = 0;
    while (written < body_data.size())
    {
        ssize_t n = ::write(fd,
                            body_data.c_str() + written,
                            body_data.size()  - written);
        if (n <= 0)
        {
            ::close(fd);
            _sendErrorInternal(500, req, cfg, wb);
            return;
        }
        written += static_cast<size_t>(n);
    }
    ::close(fd);

    // 201 Created
    // Location points to the uploaded resource
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


// ============================================================
//  _handleDelete
//
//  Removes the file at fs_path.
//  204 No Content on success; 403/500 on failure.
//  File existence is verified by the caller (handle step 6).
// ============================================================
void ResponseHandler::_handleDelete(
    const std::string&  fs_path,
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    if (::unlink(fs_path.c_str()) < 0)
    {
        if (errno == EACCES || errno == EPERM)
            _sendErrorInternal(403, req, cfg, wb);
        else
            _sendErrorInternal(500, req, cfg, wb);
        return;
    }

    // 204 No Content — no body allowed
    std::ostringstream oss;
    oss << "HTTP/1.1 204 No Content\r\n"
        << "Server: webserv/1.0\r\n"
        << "Date: "       << _httpDate()             << "\r\n"
        << "Connection: " << _connectionHeader(req)  << "\r\n"
        << "\r\n";
    _appendStr(wb, oss.str());
}


// ============================================================
//  _sendErrorInternal
//
//  Same as sendError() but uses the request's keep-alive state
//  to decide the Connection header.  Used inside handle().
// ============================================================
void ResponseHandler::_sendErrorInternal(
    int                 code,
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    std::string body = _loadErrorPage(code, cfg);

    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " " << _reasonPhrase(code) << "\r\n"
        << "Server: webserv/1.0\r\n"
        << "Date: "           << _httpDate()            << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size()            << "\r\n"
        << "Connection: "     << _connectionHeader(req) << "\r\n"
        << "\r\n"
        << body;

    _appendStr(wb, oss.str());
}


// ============================================================
//  _loadErrorPage
//
//  1. Check config.error_pages for a custom file path.
//  2. Try to read that file from disk.
//  3. Fall back to built-in HTML if missing or unreadable.
//  Inspired by nginx ngx_http_special_response.c.
// ============================================================
std::string ResponseHandler::_loadErrorPage(
    int                 code,
    const ServerConfig& cfg) const
{
    std::map<int,std::string> ep = cfg.getErrorPageMap();
    std::map<int,std::string>::const_iterator it = ep.find(code);
    if (it != ep.end() && !it->second.empty())
    {
        int fd = ::open(it->second.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            std::string content;
            char        buf[4096];
            ssize_t     n;
            while ((n = ::read(fd, buf, sizeof(buf))) > 0)
                content.append(buf, static_cast<size_t>(n));
            ::close(fd);
            if (!content.empty())
                return content;
        }
    }
    return _builtinErrorBody(code);
}


// ============================================================
//  _builtinErrorBody
//
//  Hardcoded HTML fallback — used when no custom error page
//  is configured or the configured file cannot be read.
//  Inspired by nginx ngx_http_special_response.c §4d.
// ============================================================
std::string ResponseHandler::_builtinErrorBody(int code) const
{
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n<html>\n"
        << "<head><title>" << code << " " << _reasonPhrase(code) << "</title></head>\n"
        << "<body>\n"
        << "<h1>" << code << " " << _reasonPhrase(code) << "</h1>\n"
        << "<hr><p>webserv/1.0</p>\n"
        << "</body>\n</html>\n";
    return oss.str();
}


// ============================================================
//  _writeHeaders
//
//  Writes the HTTP status-line, standard headers, any extra
//  headers, and the blank line terminator into wb.
//  The caller must append the body (or skip it for HEAD).
//
//  extra_headers must be formatted as "Key: value\r\n" lines.
// ============================================================
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
        << "Server: webserv/1.0\r\n"
        << "Date: "           << _httpDate()            << "\r\n"
        << "Content-Type: "   << ct                     << "\r\n"
        << "Content-Length: " << cl                     << "\r\n";
    if (!extra.empty())
        oss << extra;
    oss << "Connection: "     << _connectionHeader(req) << "\r\n"
        << "\r\n";
    _appendStr(wb, oss.str());
}


// ============================================================
//  _resolveFsPath
//
//  Builds the filesystem path for a request.
//  Rule (Plan.md §4): fs_path = (loc.root || cfg.root) + req.path
// ============================================================
std::string ResponseHandler::_resolveFsPath(
    const HttpRequest&  req,
    const Location*     loc,
    const ServerConfig& cfg) const
{
    std::string root = cfg.getRoot();
    if (loc && !loc->getRoot().empty())
        root = loc->getRoot();

    // Normalize: remove trailing slash(es) from root
    while (!root.empty() && root[root.size() - 1] == '/')
        root.erase(root.size() - 1);

    // req.path always starts with '/', so root + req.path is valid
    return root + req.path;
}


// ============================================================
//  _methodAllowed
// ============================================================
bool ResponseHandler::_methodAllowed(
    const std::string&              method,
    const std::vector<std::string>& allowed) const
{
    for (size_t i = 0; i < allowed.size(); ++i)
        if (allowed[i] == method)
            return true;
    return false;
}


// ============================================================
//  _getMimeType
//
//  Extension → Content-Type lookup.
//  Defaults to application/octet-stream for unknown types.
//  Inspired by nginx MIME type hash lookup.
// ============================================================
std::string ResponseHandler::_getMimeType(const std::string& path) const
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(dot);
    // Lowercase the extension for case-insensitive matching
    for (size_t i = 0; i < ext.size(); ++i)
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));

    std::map<std::string,std::string>::const_iterator it = _mime.find(ext);
    if (it != _mime.end())
        return it->second;
    return "application/octet-stream";
}


// ============================================================
//  _httpDate
//
//  Returns the current UTC time in HTTP-date format:
//  Mon, 04 Nov 2024 12:00:00 GMT
//  Uses gmtime() as required by RFC 7231.
// ============================================================
std::string ResponseHandler::_httpDate() const
{
    time_t     now = ::time(NULL);
    struct tm* gmt = ::gmtime(&now);
    char       buf[64];
    ::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
    return std::string(buf);
}


// ============================================================
//  _reasonPhrase
// ============================================================
std::string ResponseHandler::_reasonPhrase(int code) const
{
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}


// ============================================================
//  _connectionHeader
//
//  Returns "keep-alive" or "close" based on the request's
//  keep-alive state (parsed by HttpRequest::keepAlive()).
// ============================================================
std::string ResponseHandler::_connectionHeader(const HttpRequest& req) const
{
    return req.keepAlive() ? "keep-alive" : "close";
}


// ============================================================
//  _appendStr  —  buffer helpers
// ============================================================
void ResponseHandler::_appendStr(Buffer& wb, const std::string& s)
{
    wb.append(s.c_str(), s.size());
}

void ResponseHandler::_appendStr(Buffer& wb, const char* data, size_t len)
{
    wb.append(data, len);
}


// ============================================================
//  _stubCgi  —  Phase 4 seam
//
//  Called when a request matches a CGI extension.
//  Replace the body of this function with:
//
//    CgiHandler cgi(req, cfg, *loc);
//    cgi.execute(wb);
//
//  when CgiHandler is implemented in Phase 4.
// ============================================================
void ResponseHandler::_stubCgi(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    Buffer&             wb)
{
    std::cerr << "[ResponseHandler] CGI requested — Phase 4 not yet integrated\n";
    _sendErrorInternal(501, req, cfg, wb);
}

#include "Headers/HttpParser.hpp"
#include "Headers/Connection.hpp"
#include <cctype>
#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>


namespace {

enum RLState {
    RL_START,
    RL_METHOD,
    RL_SPACE_BEFORE_URI,
    RL_URI,
    RL_HTTP09,
    RL_HTTP_H,
    RL_HTTP_HT,
    RL_HTTP_HTT,
    RL_HTTP_HTTP,
    RL_FIRST_MAJOR,
    RL_MAJOR,
    RL_FIRST_MINOR,
    RL_MINOR,
    RL_SPACE_AFTER_VERSION,
    RL_ALMOST_DONE
};

enum HostState {
    HOST_START,
    HOST_IN_HOST,
    HOST_IP_LITERAL,
    HOST_IP_LITERAL_END,
    HOST_PORT
};

inline bool is_token_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9')
        || ch == '_' || ch == '-';
}

inline bool is_uri_char(unsigned char ch)
{
    return ch >= 0x20 && ch != 0x7f;
}

inline bool is_host_char(char ch)
{
    return (ch >= 'a' && ch <= 'z')
        || (ch >= 'A' && ch <= 'Z')
        || (ch >= '0' && ch <= '9')
        || ch == '-' || ch == '.'
        || ch == '_' || ch == '~'
        || ch == '!' || ch == '$' || ch == '&' || ch == '\''
        || ch == '(' || ch == ')' || ch == '*' || ch == '+'
        || ch == ',' || ch == ';' || ch == '=' || ch == '%';
}

std::string pctDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%'
            && i + 2 < s.size()
            && std::isxdigit(static_cast<unsigned char>(s[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(s[i + 2])))
        {
            unsigned char hi = static_cast<unsigned char>(s[i + 1]);
            unsigned char lo = static_cast<unsigned char>(s[i + 2]);
            int hv = std::isdigit(hi) ? hi - '0' : std::tolower(hi) - 'a' + 10;
            int lv = std::isdigit(lo) ? lo - '0' : std::tolower(lo) - 'a' + 10;
            int val = hv * 16 + lv;
            if (val == '/') {
                out += s[i];
            } else {
                out += static_cast<char>(val);
                i += 2;
            }
        }
        else
            out += s[i];
    }

    return out;
}

std::string str_tolower(const std::string& s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(r[i])));
    return r;
}

std::string str_trim(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t'))
        ++start;

    size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t'))
        --end;

    return s.substr(start, end - start);
}

bool find_crlf(const char* data, size_t len, size_t& pos)
{
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            pos = i;
            return true;
        }
    }
    return false;
}

bool validate_host(std::string& host_value, uint16_t& port_out)
{
    if (host_value.empty())
        return false;

    HostState state    = HOST_START;
    size_t    host_len = host_value.size();
    size_t    dot_pos  = host_value.size();
    int       port     = 0;
    bool      need_lc  = false;

    for (size_t i = 0; i < host_value.size(); ++i) {
        char ch = host_value[i];

        switch (state) {
            case HOST_START:
                if (ch == '[') { state = HOST_IP_LITERAL; break; }
                state = HOST_IN_HOST;
                /* fall through */

            case HOST_IN_HOST:
                if (ch >= 'A' && ch <= 'Z') { need_lc = true; break; }
                if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                    break;
                if (ch == ':') { host_len = i; state = HOST_PORT; break; }
                if (ch == '-') break;
                if (ch == '.') {
                    if (dot_pos == i - 1) return false; 
                    dot_pos = i;
                    break;
                }
                if (is_host_char(ch)) break;
                return false;

            case HOST_IP_LITERAL:
                if (ch >= 'A' && ch <= 'Z') { need_lc = true; break; }
                if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                    break;
                if (ch == ':' || ch == '-') break;
                if (ch == '.') {
                    if (dot_pos == i - 1) return false;
                    dot_pos = i;
                    break;
                }
                if (ch == ']') { host_len = i + 1; state = HOST_IP_LITERAL_END; break; }
                if (is_host_char(ch)) break;
                return false;

            case HOST_IP_LITERAL_END:
                if (ch == ':') { state = HOST_PORT; break; }
                return false;

            case HOST_PORT:
                if (ch >= '0' && ch <= '9') {
                    // Guard: port <= 65535
                    if (port > 6553 || (port == 6553 && (ch - '0') > 5))
                        return false;
                    port = port * 10 + (ch - '0');
                    break;
                }
                return false;
        }
    }

    if (state == HOST_IP_LITERAL)
        return false;

    if (host_len > 0 && host_value[host_len - 1] == '.')
        --host_len;

    if (host_len == 0)
        return false;

    std::string normalized = host_value.substr(0, host_len);
    if (need_lc)
        normalized = str_tolower(normalized);

    host_value = normalized;
    port_out   = static_cast<uint16_t>(port);
    return true;
}

}

#include <iostream>
static std::string cleanPath(const std::string& path) {
    std::string result;
    bool lastWasSlash = false;

    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '/') {
            if (!lastWasSlash) result += path[i];
            lastWasSlash = true;
        } else {
            result += path[i];
            lastWasSlash = false;
        }
    }

    // Strip trailing slash (unless root)
    if (result.size() > 1 && result[result.size() - 1] == '/')
        result.erase(result.size() - 1, 1);

    return result;
}

std::string HttpParser::_validatePath(Connection* conn)
{
    std::string root = conn->config()->getRoot();
    std::string uri  = conn->request().path;
    const Location* loc = conn->config()->matchLocation(uri);

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


void HttpParser::_applyLocationBodyLimit(Connection* conn)
{
    HttpRequest& req = conn->request();
    if (req.path.empty())
        return;

    size_t limit = conn->config()->getMaxBody();
    const Location* loc = conn->config()->matchLocation(req.path);
    if (loc && loc->getClientMaxBodySize() > 0)
        limit = loc->getClientMaxBodySize();

    if (limit == 0)
        limit = 1048576;
    req.max_body_size = limit;
    req.body.setMaxSize(limit);
    conn->readBuffer().setMaxSize(limit);
}

void HttpParser::feed(Connection *conn)
{
    if (conn->request().parse_state == PSTATE_COMPLETE ||
        conn->request().parse_state == PSTATE_ERROR)
        return;

    if (conn->request().parse_state == PSTATE_IDLE)
        conn->request().parse_state = PSTATE_REQUEST_LINE;

    if (conn->request().parse_state == PSTATE_REQUEST_LINE)
    {
        _parseRequestLine(conn);
        if (conn->request().parse_state != PSTATE_REQUEST_LINE)
            _applyLocationBodyLimit(conn);
    }

    if (conn->request().parse_state == PSTATE_HEADERS)
        _parseHeaders(conn->readBuffer(), conn->request());
    _applyLocationBodyLimit(conn);
    // if(conn->request().parse_state == PSTATE_ERROR)
    //     return;
    if (conn->request().parse_state == PSTATE_BODY) {
        if (conn->request().chunked)
            _parseChunked(conn->readBuffer(), conn->request());
        else
            _parseBody(conn->readBuffer(), conn->request());
    }
}

void HttpParser::_parseRequestLine(Connection* conn)
{
    Buffer& buf = conn->readBuffer();
    HttpRequest& req = conn->request();
    const char* const cursor   = buf.data();
    const size_t      readable = buf.size();

    if (readable == 0) return;

    const char* p   = cursor;
    const char* end = cursor + readable;

    const char* method_start = NULL;
    const char* method_end   = NULL;
    const char* uri_start    = NULL;
    const char* uri_end      = NULL;

    int  http_major = -1;
    int  http_minor = -1;
    // bool http09     = false;
    bool done       = false;

    RLState state = RL_START;

    while (p < end && !done) {
        char ch = *p;

        switch (state) {

            case RL_START:
                if (ch == '\r' || ch == '\n') { ++p; continue; }
                if (!is_token_char(ch)) {
                    req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
                }
                method_start = p;
                state = RL_METHOD;
                break;

            case RL_METHOD:
                if (ch == ' ') { method_end = p; state = RL_SPACE_BEFORE_URI; break; }
                if (!is_token_char(ch)) {
                    req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
                }
                break;

            case RL_SPACE_BEFORE_URI:
                if (ch == ' ') break;
                if (is_uri_char(static_cast<unsigned char>(ch))) {
                    uri_start = p;
                    state = RL_URI;
                    break;
                }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_URI:  
            if (ch == ' ')  { uri_end = p; state = RL_HTTP09; break; }  
            if (ch == '\r' || ch == '\n') {  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  
            if (!is_uri_char(static_cast<unsigned char>(ch))) {  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  
            break;  


            case RL_HTTP09:  
            if (ch == ' ')  break;  
            if (ch == '\r' || ch == '\n') {  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  
            if (ch == 'H')  { state = RL_HTTP_H; break; }  
            req.parse_state = PSTATE_ERROR; req.error_code = 400; return;


            case RL_HTTP_H:
                if (ch == 'T') { state = RL_HTTP_HT;   break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_HTTP_HT:
                if (ch == 'T') { state = RL_HTTP_HTT;  break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_HTTP_HTT:
                if (ch == 'P') { state = RL_HTTP_HTTP; break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_HTTP_HTTP:
                if (ch == '/') { state = RL_FIRST_MAJOR; break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_FIRST_MAJOR:
                if (ch < '0' || ch > '9') {
                    req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
                }
                http_major = ch - '0';
                state = RL_MAJOR;
                break;

            case RL_MAJOR:
                if (ch == '.')                       { state = RL_FIRST_MINOR; break; }
                if (ch >= '0' && ch <= '9') {
                    if (http_major > 9) { 
                        req.parse_state = PSTATE_ERROR; 
                        req.error_code = 400;
                        return;
                    } 
                    http_major = http_major * 10 + (ch - '0'); 
                    break; 
                }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_FIRST_MINOR:
                if (ch < '0' || ch > '9') {
                    req.parse_state = PSTATE_ERROR;
                    req.error_code = 400;
                    return;
                }
                http_minor = ch - '0';
                state = RL_MINOR;
                break;

            case RL_MINOR:
                if (ch >= '0' && ch <= '9') {
                    if (http_minor > 9)
                    {
                        req.parse_state = PSTATE_ERROR;
                        req.error_code = 400;
                        return ;
                    } 
                    http_minor = http_minor * 10 + (ch - '0'); 
                    break;
                }
                if (ch == '\r')             { state = RL_ALMOST_DONE; break; }
                if (ch == '\n')             { done = true; break; }
                if (ch == ' ')             { state = RL_SPACE_AFTER_VERSION; break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_SPACE_AFTER_VERSION:
                if (ch == ' ')  break;
                if (ch == '\r') { state = RL_ALMOST_DONE; break; }
                if (ch == '\n') { done = true; break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            case RL_ALMOST_DONE:
                if (ch == '\n') { done = true; break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
        }
        ++p;
    }

    if (!done) return;

    if (method_start == NULL || method_end == NULL || uri_start == NULL) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    if (uri_end == NULL) uri_end = p;
    if (uri_end < uri_start) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    if (http_major < 0 || http_minor < 0) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    if (!(http_major == 1 && (http_minor == 0 || http_minor == 1))) {
        req.parse_state = PSTATE_ERROR; req.error_code = 505; return;
    }

    size_t uri_len = static_cast<size_t>(uri_end - uri_start);
    if (uri_len > 8192) {
        req.parse_state = PSTATE_ERROR; req.error_code = 414; return;
    }

    req.method  = std::string(method_start,
                              static_cast<size_t>(method_end - method_start));

    req.version = "HTTP/";
    req.version += static_cast<char>('0' + http_major);
    req.version += '.';
    req.version += static_cast<char>('0' + http_minor);

    std::string raw_uri(uri_start, uri_len);
    if (raw_uri.compare(0, 7, "http://") == 0 || raw_uri.compare(0, 8, "https://") == 0) {
        size_t scheme = raw_uri.find("://");
        size_t path_start = raw_uri.find('/', scheme + 3);
        if (path_start == std::string::npos)
            raw_uri = "/";
        else
            raw_uri = raw_uri.substr(path_start);
    }


    size_t qmark = raw_uri.find('?');
    if (qmark != std::string::npos) {
        req.path         = pctDecode(raw_uri.substr(0, qmark));
        req.query_string = raw_uri.substr(qmark + 1);
    } else {
        req.path         = pctDecode(raw_uri);
        req.query_string.clear();
    }
    if (req.path.empty()) req.path = "/";
    
    std::cerr << "=======befor======> " << req.path << std::endl;
    
    req.path = cleanPath(req.path);

    std::cerr << "======after=======> " << req.path << std::endl;

    std::string abs_path = _validatePath(conn);
    std::cerr << "=============> " << abs_path << std::endl;
    struct stat info;
    
    if (stat(abs_path.c_str(), &info) != 0)
    {
        if (errno == EACCES)
        {
            conn->request().error_code = 403;
            conn->request().parse_state = PSTATE_ERROR;
        }
        else
        {
            conn->request().error_code = 404;
            conn->request().parse_state = PSTATE_ERROR;
        }
    }
    else
    {
        req.parse_state = PSTATE_HEADERS;
    }
    buf.consume(static_cast<size_t>(p - cursor));
}

static std::set<std::string> init_singleton_headers() {
    std::set<std::string> s;
    s.insert("host");
    s.insert("content-length");
    s.insert("transfer-encoding");
    s.insert("content-type");
    s.insert("content-location");
    s.insert("authorization");
    s.insert("date");
    s.insert("location");
    s.insert("retry-after");
    s.insert("max-forwards");
    s.insert("if-modified-since");
    s.insert("if-unmodified-since");
    s.insert("if-range");
    return s;
}

void HttpParser::_parseHeaders(Buffer& buf, HttpRequest& req)
{
    static const std::set<std::string> SINGLETON_HEADERS = init_singleton_headers();
    while (true) {
        if (buf.size() == 0) return;

        size_t crlf_pos;
        if (!find_crlf(buf.data(), buf.size(), crlf_pos)) return; // wait

        if (crlf_pos > 8192) {
            req.parse_state = PSTATE_ERROR; req.error_code = 431; return;
        }

        if (crlf_pos == 0) {
            buf.consume(2);
            break;
        }

        std::string line(buf.data(), crlf_pos);
        buf.consume(crlf_pos + 2);

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
        }

        std::string name  = str_tolower(str_trim(line.substr(0, colon)));
        std::string value = str_trim(line.substr(colon + 1));
        if (name.empty()) {
            req.parse_state = PSTATE_ERROR;
            req.error_code = 400;
            return;
        }
        else if (req.headers.find(name) != req.headers.end())
        {
             if (SINGLETON_HEADERS.find(name) != SINGLETON_HEADERS.end()) {
                req.parse_state = PSTATE_ERROR;
                req.error_code = 400;
                return;
            }
            req.headers[name] += ", " + value;
        }
        else
            req.headers[name] = value;
        if (req.headers.size() > 100) {
            req.parse_state = PSTATE_ERROR;
            req.error_code = 431;
            return;
        }
    }


    if (req.version == "HTTP/1.1" &&
        req.headers.find("host") == req.headers.end()) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    if (req.headers.find("host") != req.headers.end()) {
        std::string host_val = req.headers["host"];
        uint16_t    port     = 0;
        if (!validate_host(host_val, port)) {
            req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
        }
        req.headers["host"] = host_val;
    }
    if (req.headers.find("content-length") != req.headers.end() && 
        req.headers.find("transfer-encoding") != req.headers.end())  
    {  
        req.parse_state = PSTATE_ERROR;  
        req.error_code  = 400;  
        return;  
    }  
    std::map<std::string, std::string>::const_iterator it =
        req.headers.find("content-length");
    if (it != req.headers.end()) {
        const std::string& cl_str = it->second;
        if (cl_str.empty()) {
            req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
        }
        size_t cl = 0;
        for (size_t i = 0; i < cl_str.size(); ++i) {
            if (cl_str[i] < '0' || cl_str[i] > '9') {
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
            }
            if (cl > SIZE_MAX / 10)
            {
                req.parse_state = PSTATE_ERROR;
                req.error_code = 400;
                return ;
            }
            cl = cl * 10 + static_cast<size_t>(cl_str[i] - '0');
        }
        req.content_length = cl;
    }

    it = req.headers.find("transfer-encoding");
    if (it != req.headers.end()) {
        if (str_tolower(it->second) == "chunked") {
            req.chunked        = true;
            req.content_length = 0;
        }
    }

    if (!req.chunked && req.content_length == 0)
    {
        if (req.method == "POST")
        {
            req.parse_state = PSTATE_ERROR;
            req.error_code = 411;
        }
        else
            req.parse_state = PSTATE_COMPLETE;
    }
    else
        req.parse_state = PSTATE_BODY;

    

}

void HttpParser::_parseBody(Buffer& buf, HttpRequest& req)
{
    if (buf.size() == 0) return;

    if (req.content_length == 0 && !req.chunked)
    {
        size_t to_read = buf.size();
        if (req.written + to_read > req.max_body_size)
        {
            req.parse_state = PSTATE_ERROR;
            req.error_code  = 413;
            return;
        }
        req.body.append(buf.data(), to_read);
        buf.consume(to_read);
        req.written += to_read;
        return;
    }
    
    size_t already   = req.written;
    size_t needed    = req.content_length - already;
    size_t available = buf.size();
    size_t to_read   = (needed < available) ? needed : available;
    if (req.written + to_read > req.max_body_size)  
    {  
        req.parse_state = PSTATE_ERROR;  
        req.error_code = 413;  
        return;  
    } 
    req.body.append(buf.data(), to_read);
    buf.consume(to_read);
    req.written += to_read;
    if (req.written == req.content_length)
    {
        buf.earase();
        req.parse_state = PSTATE_COMPLETE;
    }
}

void HttpParser::_parseChunked(Buffer& buf, HttpRequest& req)
{
    while (true) {

        if (req._chunk_trailing) {
            if (buf.size() < 2) return;
            buf.consume(2);
            req._chunk_trailing = false;

            if (req._chunk_done) {
                req.parse_state = PSTATE_COMPLETE;
                return;
            }
            req._chunk_size = 0;
        }

        if (req._chunk_size == 0) {
            size_t crlf_pos;
            if (!find_crlf(buf.data(), buf.size(), crlf_pos)) return;

            std::string hex_line(buf.data(), crlf_pos);
            buf.consume(crlf_pos + 2);

            if (hex_line.empty()) {
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
            }

            size_t chunk_sz = 0;  
            bool   valid    = false;  
            size_t ext_start = hex_line.size();
            for (size_t i = 0; i < hex_line.size(); ++i) {  
                char c = hex_line[i];  
                if (c == ';') { ext_start = i; break; }  
                unsigned int d;  
                if (c >= '0' && c <= '9')      d = static_cast<unsigned int>(c - '0');  
                else if (c >= 'a' && c <= 'f') d = static_cast<unsigned int>(c - 'a') + 10;  
                else if (c >= 'A' && c <= 'F') d = static_cast<unsigned int>(c - 'A') + 10;  
                else {  
                    req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
                }
                const size_t MAX_ALLOWED_CHUNK = 1073741824ULL; 
                if (chunk_sz > (MAX_ALLOWED_CHUNK / 16)) {
                std::cerr << "---------------------[HttpParser] body limit exceeded on fd 9999999 ----------------" << "\n";

                    req.parse_state = PSTATE_ERROR; 
                    req.error_code = 413; 
                    return;  
                }
                size_t next_val = chunk_sz * 16 + d;
                if (next_val > MAX_ALLOWED_CHUNK) {
                    req.parse_state = PSTATE_ERROR; 
                    req.error_code = 413; 
                    return;  
                }
                chunk_sz = next_val;
                valid = true;
            }  
            if (!valid) {  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  

            for (size_t i = ext_start; i < hex_line.size(); ++i) {  
                unsigned char uc = static_cast<unsigned char>(hex_line[i]);  
                if (uc < 0x20 && uc != 0x09) {
                    req.parse_state = PSTATE_ERROR;  
                    req.error_code  = 400;  
                    return;  
                }  
                if (uc == 0x7F) { // DEL  
                    req.parse_state = PSTATE_ERROR;  
                    req.error_code  = 400;  
                    return;  
                }  
            }

            if (chunk_sz == 0) {
                req._chunk_done     = true;
                req._chunk_trailing = true;
                continue;
            }

            req._chunk_size = chunk_sz;
        }

        if (buf.size() == 0) return;

        size_t to_read =
            (req._chunk_size < buf.size()) ? req._chunk_size : buf.size();
        
        if (req.written + to_read > req.max_body_size)  
        {
            std::cerr << "req.max_body_size |||| = " << req.max_body_size << "\n";
            std::cerr << "req.written + to_read |||| = " << req.written + to_read<< "\n";
            std::cerr << "---------------------[HttpParser] body limit exceeded on fd 333333 ----------------"<< "\n";
            req.parse_state = PSTATE_ERROR;  
            req.error_code = 413;  
            return;  
        }  
        req.body.append(buf.data(), to_read);
        buf.consume(to_read);
        req.written += to_read;
        req._chunk_size -= to_read;

        if (req._chunk_size == 0) {
            req._chunk_trailing = true;
            req._chunk_done     = false;
        }
    }
}

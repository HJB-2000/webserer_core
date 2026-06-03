// ============================================================
//  HttpParser.cpp
//  HTTP/1.x request parser — Phase 2 implementation.
//
//  Logic sources (adapted, not copied verbatim):
//    http_parser/http_parser_entry.cpp  → request-line state machine
//    http_parser/http_headers.cpp       → header line scanning
//    http_parser/http_uri.cpp           → URI / query-string split
//    http_parser/validate_host.cpp      → Host header validation
//
//  Design:
//    All state lives in HttpRequest.  HttpParser itself has no
//    member variables — one instance in EventLoop serves all
//    connections without any per-connection locking or copying.
// ============================================================

#include "Headers/HttpParser.hpp"
#include "Headers/Connection.hpp"
#include <cctype>    // std::tolower
#include <stdint.h>  // uint16_t (C++98 compatible)
#include <map>
#include <set>
#include <string>

// ── anonymous namespace: file-local helpers ──────────────────
namespace {

// ── request-line parse states ────────────────────────────────
// Mirrors the `gram` enum from http_parser/http_parser.hpp
// but scoped here so it does not pollute the global namespace.
enum RLState {
    RL_START,
    RL_METHOD,
    RL_SPACE_BEFORE_URI,
    RL_URI,
    RL_HTTP09,          // space(s) after URI, deciding HTTP/0.9 vs versioned
    RL_HTTP_H,
    RL_HTTP_HT,
    RL_HTTP_HTT,
    RL_HTTP_HTTP,
    RL_FIRST_MAJOR,
    RL_MAJOR,
    RL_FIRST_MINOR,
    RL_MINOR,
    RL_SPACE_AFTER_VERSION,
    RL_ALMOST_DONE      // seen \r, waiting for \n
};

// ── host-validation states ───────────────────────────────────
enum HostState {
    HOST_START,
    HOST_IN_HOST,
    HOST_IP_LITERAL,
    HOST_IP_LITERAL_END,
    HOST_PORT
};

// ── character classification ─────────────────────────────────

// HTTP token character (method name alphabet)
// RFC 7230 §3.2.6 — letters, digits, and select symbols.
// The original nginx parser uses letters, digits, '-', '_'.
inline bool is_token_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9')
        || ch == '_' || ch == '-';
}

// Valid URI octet: printable ASCII, excluding DEL (0x7f).
inline bool is_uri_char(unsigned char ch)
{
    return ch >= 0x20 && ch != 0x7f;
}

// Valid host character (unreserved or sub-delimiter per RFC 3986).
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

// ── pctDecode ────────────────────────────────────────────────
//
// Decodes percent-encoded characters in a URI path component.
// RFC 3986 §2.1: %XX where XX is a hex pair.
//
// Safety: %2F (encoded '/') is intentionally NOT decoded —
// decoding it would allow clients to escape the server root
// by disguising path separators (path traversal).
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
            if (val == '/') {          // keep %2F encoded — never decode
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

// ── string helpers ───────────────────────────────────────────

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

// Find the first \r\n in [data, data+len).
// Returns true and sets pos to the index of \r on success.
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

// ── host validation (from http_parser/validate_host.cpp) ─────
//
// Validates and normalises a Host header value.
// Strips trailing dot, lowercases the hostname, extracts port.
// Returns false if the value is malformed.
bool validate_host(std::string& host_value, uint16_t& port_out)
{
    if (host_value.empty())
        return false;

    HostState state    = HOST_START;
    size_t    host_len = host_value.size();
    size_t    dot_pos  = host_value.size(); // sentinel "no consecutive dot"
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
                    if (dot_pos == i - 1) return false; // consecutive dots
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
        return false; // unclosed '['

    // Strip trailing dot (e.g. "example.com.")
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

} // anonymous namespace

#include <iostream>
// ════════════════════════════════════════════════════════════
//  HttpParser::feed
//  Main entry point — advance parse as far as the buffer allows.
// ════════════════════════════════════════════════════════════
void HttpParser::feed(Connection *conn)
{
    // Nothing to do if already terminal.
    
    if (conn->request().parse_state == PSTATE_COMPLETE ||
        conn->request().parse_state == PSTATE_ERROR)
        return;

    // First byte of a new request kicks us out of IDLE.
    if (conn->request().parse_state == PSTATE_IDLE)
        conn->request().parse_state = PSTATE_REQUEST_LINE;

    if (conn->request().parse_state == PSTATE_REQUEST_LINE)
    {
        _parseRequestLine(conn->readBuffer(), conn->request());
        const Location* loc = conn->config()->matchLocation(conn->request().path);    
        if (loc && loc->getClientMaxBodySize() > 0)  
        {
            conn->request().max_body_size = loc->getClientMaxBodySize();    
        }  
        else
        {
            conn->request().max_body_size = conn->config()->getMaxBody();  
        }    
        // Body limit enforced in _parseBody()/_parseChunked(), not Buffer::append()
    }

    if (conn->request().parse_state == PSTATE_HEADERS)
        _parseHeaders(conn->readBuffer(), conn->request()); 

    if (conn->request().parse_state == PSTATE_BODY) {
        if (conn->request().chunked)
            _parseChunked(conn->readBuffer(), conn->request());
        else
            _parseBody(conn->readBuffer(), conn->request());
    }
}


// ════════════════════════════════════════════════════════════
//  HttpParser::_parseRequestLine
//
//  Parses "METHOD SP URI SP HTTP/major.minor CRLF".
//
//  Character-by-character state machine adapted from
//  http_parser/http_parser_entry.cpp :: parse_request_line().
//
//  On success: fills req.method, req.path, req.query_string,
//              req.version; consumes the line from buf;
//              advances req.parse_state → PSTATE_HEADERS.
//  On partial: returns immediately (nothing consumed).
//  On error:   sets PSTATE_ERROR + error_code; nothing consumed.
// ════════════════════════════════════════════════════════════
void HttpParser::_parseRequestLine(Buffer& buf, HttpRequest& req)
{
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

            // ── leading whitespace (CR/LF between pipelined reqs) ──
            case RL_START:
                if (ch == '\r' || ch == '\n') { ++p; continue; }
                if (!is_token_char(ch)) {
                    req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
                }
                method_start = p;
                state = RL_METHOD;
                break;

            // ── method token ─────────────────────────────────────
            case RL_METHOD:
                if (ch == ' ') { method_end = p; state = RL_SPACE_BEFORE_URI; break; }
                if (!is_token_char(ch)) {
                    req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
                }
                break;

            // ── space(s) before URI ───────────────────────────────
            case RL_SPACE_BEFORE_URI:
                if (ch == ' ') break; // absorb extra spaces
                if (is_uri_char(static_cast<unsigned char>(ch))) {
                    uri_start = p;
                    state = RL_URI;
                    break;
                }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;

            // ── URI ───────────────────────────────────────────────
            case RL_URI:  
            if (ch == ' ')  { uri_end = p; state = RL_HTTP09; break; }  
            if (ch == '\r' || ch == '\n') {  
                // No HTTP version → malformed request (HTTP/0.9 not supported)  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  
            if (!is_uri_char(static_cast<unsigned char>(ch))) {  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  
            break;  


            // ── deciding HTTP/0.9 vs versioned ────────────────────
            case RL_HTTP09:  
            if (ch == ' ')  break;  
            if (ch == '\r' || ch == '\n') {  
                // No HTTP version after URI + space → malformed  
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;  
            }  
            if (ch == 'H')  { state = RL_HTTP_H; break; }  
            req.parse_state = PSTATE_ERROR; req.error_code = 400; return;


            // ── "HTTP/" ───────────────────────────────────────────
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

            // ── major version digit(s) ────────────────────────────
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

            // ── minor version digit(s) ────────────────────────────
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

            // ── \r seen — expect \n ───────────────────────────────
            case RL_ALMOST_DONE:
                if (ch == '\n') { done = true; break; }
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
        }
        ++p;
    }

    if (!done) return; // need more data — nothing consumed yet

    // ── sanity checks on the parsed tokens ───────────────────
    if (method_start == NULL || method_end == NULL || uri_start == NULL) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    if (uri_end == NULL) uri_end = p; // no version: end = current position
    if (uri_end < uri_start) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    if (http_major < 0 || http_minor < 0) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    // Only HTTP/1.0 and HTTP/1.1
    if (!(http_major == 1 && (http_minor == 0 || http_minor == 1))) {
        req.parse_state = PSTATE_ERROR; req.error_code = 505; return;
    }

    // URI length guard (RFC 7231 §6.5.12 — 414 URI Too Long)
    size_t uri_len = static_cast<size_t>(uri_end - uri_start);
    if (uri_len > 8192) {
        req.parse_state = PSTATE_ERROR; req.error_code = 414; return;
    }

    // ── fill HttpRequest fields ───────────────────────────────
    req.method  = std::string(method_start,
                              static_cast<size_t>(method_end - method_start));

    req.version = "HTTP/";
    req.version += static_cast<char>('0' + http_major);
    req.version += '.';
    req.version += static_cast<char>('0' + http_minor);

    // Split URI into path and query string, then percent-decode the path.
    // Query string is left encoded — CGI/app decodes its own parameters.
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

    // Consume the entire request line from the buffer
    buf.consume(static_cast<size_t>(p - cursor));
    req.parse_state = PSTATE_HEADERS;
}


// ════════════════════════════════════════════════════════════
//  HttpParser::_parseHeaders
//
//  Reads header lines one at a time, consuming each from the
//  buffer as soon as it is complete.  Returns as soon as a
//  line boundary (\r\n) is not yet in the buffer.
//
//  Adapted from http_parser/http_headers.cpp ::
//  http_process_request_headers() — rewritten to consume
//  incrementally instead of scanning the full buffer each call.
// ════════════════════════════════════════════════════════════
static std::set<std::string> init_singleton_headers() {
    std::set<std::string> s;
    // Tier 1 — smuggling vectors
    s.insert("host");
    s.insert("content-length");
    s.insert("transfer-encoding");
    // Tier 2 — RFC singleton enforcement
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

        // ── check line length before allocating ──────────────
        if (crlf_pos > 8192) {
            req.parse_state = PSTATE_ERROR; req.error_code = 431; return;
        }

        // ── empty line = end of headers ──────────────────────
        if (crlf_pos == 0) {
            buf.consume(2);
            break;
        }

        std::string line(buf.data(), crlf_pos);
        buf.consume(crlf_pos + 2);

        // ── split on first ':' ────────────────────────────────
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
        // Header count guard (RFC 7231 — 431 Request Header Fields Too Large)
        if (req.headers.size() > 100) {
            req.parse_state = PSTATE_ERROR;
            req.error_code = 431;
            return;
        }
    }

    // ── headers complete — post-parse checks ─────────────────

    // HTTP/1.1 MUST have a Host header (RFC 7230 §5.4)
    if (req.version == "HTTP/1.1" &&
        req.headers.find("host") == req.headers.end()) {
        req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
    }

    // Validate and extract Host
    if (req.headers.find("host") != req.headers.end()) {
        std::string host_val = req.headers["host"];
        uint16_t    port     = 0;
        if (!validate_host(host_val, port)) {
            req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
        }
        // Store the normalised host back into headers
        req.headers["host"] = host_val;
    }
    if (req.headers.find("content-length") != req.headers.end() && 
        req.headers.find("transfer-encoding") != req.headers.end())  
    {  
        req.parse_state = PSTATE_ERROR;  
        req.error_code  = 400;  
        return;  
    }  
    // Content-Length
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

    // Transfer-Encoding: chunked (takes precedence over Content-Length)
    it = req.headers.find("transfer-encoding");
    if (it != req.headers.end()) {
        if (str_tolower(it->second) == "chunked") {
            req.chunked        = true;
            req.content_length = 0; // irrelevant when chunked
        }
    }

    // Decide what comes next
    if (!req.chunked && req.content_length == 0)
        req.parse_state = PSTATE_COMPLETE;
    else
        req.parse_state = PSTATE_BODY;
}


// ════════════════════════════════════════════════════════════
//  HttpParser::_parseBody
//  Fixed-length body (Content-Length).
// ════════════════════════════════════════════════════════════
void HttpParser::_parseBody(Buffer& buf, HttpRequest& req)
{
    if (buf.size() == 0) return;

    size_t already   = req.body.size();
    size_t needed    = req.content_length - already;
    size_t available = buf.size();
    size_t to_read   = (needed < available) ? needed : available;
    if (req.body.size() + to_read > req.max_body_size)  // ← Add this  
    {  
        req.parse_state = PSTATE_ERROR;  
        req.error_code = 413;  
        return;  
    } 
    req.body.append(buf.data(), to_read);
    buf.consume(to_read);

    if (req.body.size() == req.content_length)
        req.parse_state = PSTATE_COMPLETE;
}


// ════════════════════════════════════════════════════════════
//  HttpParser::_parseChunked
//  Chunked transfer-encoding body decoder.
//
//  Wire format per RFC 7230 §4.1:
//
//      chunk-size\r\n          ← hex digits, optional extensions after ';'
//      chunk-data\r\n
//      ...
//      0\r\n                   ← terminal chunk
//      \r\n                    ← final empty line
//
//  State is kept in three HttpRequest fields so the method can
//  be suspended and resumed across recv() boundaries:
//
//      _chunk_size     > 0  → reading chunk data bytes
//                      = 0  → need to read next size line
//      _chunk_trailing true → consuming the 2-byte trailing CRLF
//      _chunk_done     true → the trailing CRLF is the terminal one
//                             → PSTATE_COMPLETE after consuming it
// ════════════════════════════════════════════════════════════
void HttpParser::_parseChunked(Buffer& buf, HttpRequest& req)
{
    while (true) {

        // ── step 1: consume trailing CRLF ────────────────────
        if (req._chunk_trailing) {
            if (buf.size() < 2) return; // wait for \r\n
            buf.consume(2);
            req._chunk_trailing = false;

            if (req._chunk_done) {
                // The CRLF we just consumed ended the terminal chunk
                req.parse_state = PSTATE_COMPLETE;
                return;
            }
            // Regular chunk finished — fall through to read next size line
            req._chunk_size = 0;
        }

        // ── step 2: read chunk size line ─────────────────────
        if (req._chunk_size == 0) {
            size_t crlf_pos;
            if (!find_crlf(buf.data(), buf.size(), crlf_pos)) return; // wait

            std::string hex_line(buf.data(), crlf_pos);
            buf.consume(crlf_pos + 2);

            if (hex_line.empty()) {
                // Empty size line is malformed
                req.parse_state = PSTATE_ERROR; req.error_code = 400; return;
            }

            // Parse hex digits; stop at ';' (chunk extension) or end of line  
            size_t chunk_sz = 0;  
            bool   valid    = false;  
            size_t ext_start = hex_line.size(); // no extension by default  
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
                // Check BEFORE math operations
                if (chunk_sz > (MAX_ALLOWED_CHUNK / 16)) {
                    req.parse_state = PSTATE_ERROR; 
                    req.error_code = 413; 
                    return;  
                }
                // Now safe to multiply
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
  
            // ── RFC 7230 §4.1.1: validate chunk extensions ──────  
            // Reject control chars (except HTAB) in the extension portion.  
            // We don't need to interpret the extensions, just ensure they  
            // aren't carrying garbage bytes.  
            for (size_t i = ext_start; i < hex_line.size(); ++i) {  
                unsigned char uc = static_cast<unsigned char>(hex_line[i]);  
                if (uc < 0x20 && uc != 0x09) { // control char that isn't HTAB  
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
                // Terminal chunk: must consume one more \r\n then we are done
                req._chunk_done     = true;
                req._chunk_trailing = true;
                continue; // loop back to step 1 to consume the final CRLF
            }

            req._chunk_size = chunk_sz;
        }

        // ── step 3: read chunk data ───────────────────────────
        if (buf.size() == 0) return;

        size_t to_read =
            (req._chunk_size < buf.size()) ? req._chunk_size : buf.size();
        if (req.body.size() + to_read > req.max_body_size)  
        {  
            req.parse_state = PSTATE_ERROR;  
            req.error_code = 413;  
            return;  
        }  
        req.body.append(buf.data(), to_read);
        buf.consume(to_read);
        req._chunk_size -= to_read;

        if (req._chunk_size == 0) {
            // All data bytes for this chunk are in; now expect trailing CRLF
            req._chunk_trailing = true;
            req._chunk_done     = false;
            // Loop to try consuming it immediately if already buffered
        }
    }
}

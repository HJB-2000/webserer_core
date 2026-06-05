// ============================================================
//  HttpRequest.cpp — HttpRequest implementations
// ============================================================
#include "Headers/HttpRequest.hpp"
#include <unistd.h>
#include <cctype>

HttpRequest::HttpRequest()
    :body_fd(-1)
    ,body_size(0)
    ,parse_state(PSTATE_IDLE)
    , content_length(0)
    , chunked(false)
    , max_body_size(0)
    , error_code(0)
    , _chunk_size(0)
    , _chunk_trailing(false)
    , _chunk_done(false)
{}

void HttpRequest::reset()
{
    if (body_fd >= 0) { ::close(body_fd); body_fd = -1; }
    body_size = 0;
    body.clear(); // keep field, always empty now
    method.clear();
    path.clear();
    query_string.clear();
    version.clear();
    headers.clear();
    body.clear();
    parse_state     = PSTATE_IDLE;
    content_length  = 0;
    chunked         = false;
    error_code      = 0;
    _chunk_size     = 0;
    _chunk_trailing = false;
    _chunk_done     = false;
    max_body_size   = 0;
}

std::string HttpRequest::header(const std::string& key) const
{
    std::map<std::string, std::string>::const_iterator it
        = headers.find(key);
    return (it != headers.end()) ? it->second : std::string();
}

bool HttpRequest::expectsBody() const
{
    return (content_length > 0 || chunked);
}

bool HttpRequest::keepAlive() const
{
    std::string conn = header("connection");
    for (size_t i = 0; i < conn.size(); ++i)
        conn[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(conn[i])));
    if (version == "HTTP/1.1")
        return (conn != "close");
    return (conn == "keep-alive");
}

#include "Headers/HttpRequest.hpp"
#include <cctype>
#include <unistd.h>
#include <cstdio>
HttpRequest::HttpRequest(size_t client_max_body_size)
    : body(client_max_body_size)
    , parse_state(PSTATE_IDLE)
    , content_length(0)
    , chunked(false)
    , max_body_size(0)
    , error_code(0)
    , _chunk_size(0)
    , _chunk_trailing(false)
    , _chunk_done(false)
    , opened(false)
    , opened_file(-1)
    , written(0)
    , body_file_written(0)
{}

void HttpRequest::reset()
{
    method.clear();
    path.clear();
    query_string.clear();
    version.clear();
    headers.clear();
    body.reset();
    if (opened_file >= 0)
    {
        ::close(opened_file);
        opened_file = -1;
    }
    if (!tmp_body_path.empty())
    {
        std::remove(tmp_body_path.c_str());
        tmp_body_path.clear();
    }
    opened            = false;
    body_file_written = 0;
    parse_state     = PSTATE_IDLE;
    content_length  = 0;
    chunked         = false;
    error_code      = 0;
    _chunk_size     = 0;
    _chunk_trailing = false;
    _chunk_done     = false;
    max_body_size   = 0;
    written         = 0;
}

std::string HttpRequest::header(const std::string& key) const
{
    std::map<std::string, std::string>::const_iterator it
        = headers.find(key);
    return (it != headers.end()) ? it->second : std::string();
}

bool HttpRequest::expectsBody() const
{
    if (content_length > 0 || chunked)
        return true;
    if (method == "POST")
        return true;
    return false;
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

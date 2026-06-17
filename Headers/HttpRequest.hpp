#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP
#include "buffer.hpp"
#include <string>
#include <map>
#include <cstddef>

enum ParseState
{
    PSTATE_IDLE         = 0,
    PSTATE_REQUEST_LINE,
    PSTATE_HEADERS,
    PSTATE_BODY,
    PSTATE_COMPLETE,
    PSTATE_ERROR
};

class HttpRequest
{
public:

    std::string method;
    std::string path;
    std::string query_string;
    std::string version;

    std::map<std::string, std::string> headers;

    Buffer body;

    ParseState  parse_state;
    size_t      content_length;
    bool        chunked;
    size_t      max_body_size;

    int         error_code;

    size_t      _chunk_size;      
    bool        _chunk_trailing;  
    bool        _chunk_done;      
    
    HttpRequest(size_t client_max_body_size);
    void reset();
    
    std::string header(const std::string& key) const;
    bool        expectsBody()                  const;
    bool        keepAlive()                    const;
    bool opened;
    int opened_file;
    std::string tmp_body_path;
    size_t written;
    size_t body_file_written;
};

#endif

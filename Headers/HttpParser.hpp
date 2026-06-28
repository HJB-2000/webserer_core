#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "Connection.hpp"

class HttpParser
{
public:

    void feed(Connection *conn);

private:

    void _applyLocationBodyLimit(Connection* conn);
    void _parseRequestLine(Connection* conn);
    std::string _validatePath(Connection* conn);

    // void _parseHeaders(Buffer& buf, HttpRequest& req);
    void _parseHeaders(Connection *conn);

    void _parseBody(Buffer& buf, HttpRequest& req);

    void _parseChunked(Buffer& buf, HttpRequest& req);
};

#endif

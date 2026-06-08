#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "Headers/Connection.hpp"

class HttpParser
{
public:

    void feed(Connection *conn);

private:

    void _parseRequestLine(Buffer& buf, HttpRequest& req);

    void _parseHeaders(Buffer& buf, HttpRequest& req);

    void _parseBody(Buffer& buf, HttpRequest& req);

    void _parseChunked(Buffer& buf, HttpRequest& req);
};

#endif

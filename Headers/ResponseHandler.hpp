#ifndef RESPONSE_HANDLER_HPP
#define RESPONSE_HANDLER_HPP

#include <string>
#include <map>
#include <vector>

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "serverConfig.hpp"
#include "CgiRequestInfo.hpp"

class ResponseHandler
{
public:

    ResponseHandler();

    void handle(
        const HttpRequest&  request,
        const ServerConfig& config,
        Buffer&             write_buffer
    );

    void sendError(
        int                 status_code,
        const ServerConfig& config,
        Buffer&             write_buffer
    );

    bool resolveCgiRequest(
        const HttpRequest&  req,
        const ServerConfig& cfg,
        CgiRequestInfo&     out
    ) const;
    
    void handleCgiOutput(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    const Buffer&       cgi_output,
    Buffer&             wb
    );

private:

    ResponseHandler(const ResponseHandler&);
    ResponseHandler& operator=(const ResponseHandler&);


    void _serveStaticFile(
        const HttpRequest&  req,
        const std::string&  fs_path,
        const ServerConfig& cfg,
        Buffer&             wb
    );

    void _sendDirectoryListing(
        const HttpRequest&  req,
        const std::string&  fs_path,
        const ServerConfig& cfg,
        Buffer&             wb
    );

    void _sendRedirect(
        int                 code,
        const std::string&  url,
        const HttpRequest&  req,
        Buffer&             wb
    );

    void _handlePost(
        const HttpRequest&  req,
        const Location&     loc,
        const ServerConfig& cfg,
        Buffer&             wb
    );

    void _handleDelete(
        const std::string&  fs_path,
        const HttpRequest&  req,
        const ServerConfig& cfg,
        Buffer&             wb
    );


    void _sendErrorInternal(
        int                 code,
        const HttpRequest&  req,
        const ServerConfig& cfg,
        Buffer&             wb
    );

    std::string _loadErrorPage(int code, const ServerConfig& cfg) const;
    std::string _builtinErrorBody(int code)                        const;


    void _writeHeaders(
        int                 code,
        const std::string&  content_type,
        size_t              content_length,
        const std::string&  extra_headers,  
        const HttpRequest&  req,
        Buffer&             wb
    );


    std::string _resolveFsPath(
        const HttpRequest&  req,
        const Location*     loc,
        const ServerConfig& cfg
    ) const;

    bool _methodAllowed(
        const std::string&              method,
        const std::vector<std::string>& allowed
    ) const;


    std::string _getMimeType(const std::string& path) const;
    std::string _httpDate()                            const;
    std::string _reasonPhrase(int code)                const;
    std::string _connectionHeader(const HttpRequest&)  const;


    void _appendStr(Buffer& wb, const std::string& s);
    void _appendStr(Buffer& wb, const char* data, size_t len);

    std::map<std::string, std::string> _mime;
};

std::string htmlEscape(const std::string& s);

#endif

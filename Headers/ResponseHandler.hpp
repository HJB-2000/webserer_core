// ============================================================
//  ResponseHandler.hpp  —  Phase 3
//
//  Reads a complete HttpRequest + ServerConfig, decides what
//  the response should be, builds it, and writes raw bytes
//  into write_buffer.  That is its entire job.
//
//  Interface matches Plan.md §4 exactly.
//
//  Integration seams in EventLoop._handleRead():
//    _stubBuildResponse() → _responder.handle(...)
//    _stubSend400()       → _responder.sendError(error_code, ...)
//    _stub413()           → _responder.sendError(413, ...)
//
//  Phase 4 seam inside handle():
//    _stubCgi()           → CgiHandler::execute(...)
//
//  Dependency chain:
//    Buffer → HttpRequest → ServerConfig → ResponseHandler
// ============================================================
#ifndef RESPONSE_HANDLER_HPP
#define RESPONSE_HANDLER_HPP

#include <string>
#include <map>
#include <vector>

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "serverConfig.hpp"

class ResponseHandler
{
public:

    ResponseHandler();

    // ── public interface ──────────────────────────────────────
    //
    // handle()    : full request → decide + build response
    // sendError() : called before a full request is available
    //               (parse errors 400/414/431/505, body limit 413)
    //               always uses Connection: close.

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

private:

    // ── non-copyable ──────────────────────────────────────────
    ResponseHandler(const ResponseHandler&);
    ResponseHandler& operator=(const ResponseHandler&);

    // ── per-method / per-feature handlers ────────────────────

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

    // ── error helpers ─────────────────────────────────────────

    // Like sendError() but uses the request's keep-alive state
    // for the Connection header.  Called from within handle().
    void _sendErrorInternal(
        int                 code,
        const HttpRequest&  req,
        const ServerConfig& cfg,
        Buffer&             wb
    );

    std::string _loadErrorPage(int code, const ServerConfig& cfg) const;
    std::string _builtinErrorBody(int code)                        const;

    // ── response assembly ─────────────────────────────────────

    // Writes status-line + standard headers + extra_headers + blank line.
    // Caller appends the body separately (or skips it for HEAD).
    void _writeHeaders(
        int                 code,
        const std::string&  content_type,
        size_t              content_length,
        const std::string&  extra_headers,  // must end with \r\n if non-empty
        const HttpRequest&  req,
        Buffer&             wb
    );

    // ── path helpers ──────────────────────────────────────────

    std::string _resolveFsPath(
        const HttpRequest&  req,
        const Location*     loc,
        const ServerConfig& cfg
    ) const;

    bool _methodAllowed(
        const std::string&              method,
        const std::vector<std::string>& allowed
    ) const;

    // ── HTTP helpers ──────────────────────────────────────────

    std::string _getMimeType(const std::string& path) const;
    std::string _httpDate()                            const;
    std::string _reasonPhrase(int code)                const;
    std::string _connectionHeader(const HttpRequest&)  const;

    // ── buffer helpers ────────────────────────────────────────

    void _appendStr(Buffer& wb, const std::string& s);
    void _appendStr(Buffer& wb, const char* data, size_t len);

    // ── Phase 4 seam ──────────────────────────────────────────
    // Replace body of _stubCgi() with CgiHandler::execute() call.
    void _stubCgi(
        const HttpRequest&  req,
        const ServerConfig& cfg,
        Buffer&             wb
    );

    // ── MIME table (populated in constructor) ─────────────────
    std::map<std::string, std::string> _mime;
};

#endif // RESPONSE_HANDLER_HPP

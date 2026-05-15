// ============================================================
//  HttpParser.hpp
//  Stateless HTTP/1.x request parser.
//  C++98 compliant.
//
//  Interface:
//      void feed(Buffer& read_buffer, HttpRequest& request)
//
//  Called by EventLoop::_handleRead() after every recv().
//  All parse state lives in HttpRequest, so a single HttpParser
//  instance in EventLoop can serve every Connection safely.
//
//  Dependency chain:
//      buffer.hpp       → Buffer
//      HttpRequest.hpp  → HttpRequest, ParseState
//          └── HttpParser.hpp  ← we are here
// ============================================================
#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "Headers/Connection.hpp"

// ────────────────────────────────────────────────────────────
//  HttpParser
//
//  Stateless — all progress is tracked in HttpRequest fields:
//      parse_state      which phase we are in
//      content_length   body byte count (fixed-length)
//      chunked          true → chunked transfer encoding
//      _chunk_size      bytes left in the current chunk
//      _chunk_trailing  waiting to consume post-chunk CRLF
//      _chunk_done      trailing CRLF belongs to terminal chunk
//
//  feed() is resumable: if the buffer does not yet contain
//  enough bytes to complete the current phase it returns
//  without consuming those bytes, leaving them for the next
//  recv() → feed() cycle.
//
//  On a protocol violation feed() sets parse_state = PSTATE_ERROR
//  and error_code to the appropriate HTTP status code.
//  The caller (EventLoop) checks parse_state after each call.
// ────────────────────────────────────────────────────────────
class HttpParser
{
public:

    // ── main entry point ──────────────────────────────────
    /**
     * Advance the parse as far as the buffer allows.
     *
     * @param buf   Connection's read buffer (partially consumed
     *              by each phase as bytes are processed).
     * @param req   The request object being filled in.
     *
     * Postconditions (one per call):
     *   req.parse_state == PSTATE_COMPLETE → full request ready
     *   req.parse_state == PSTATE_ERROR    → protocol violation;
     *       req.error_code carries the HTTP status code (400/414/431/505)
     *   otherwise                          → partial; wait for more data
     */
    void feed(Connection *conn);

private:

    // ── phase parsers ─────────────────────────────────────

    /**
     * Parse "METHOD SP URI SP HTTP/x.y CRLF".
     * On success: fills req.method, req.path, req.query_string,
     *             req.version; advances state to PSTATE_HEADERS.
     * On partial: returns without consuming; wait for more data.
     * On error:   sets PSTATE_ERROR + error_code.
     */
    void _parseRequestLine(Buffer& buf, HttpRequest& req);

    /**
     * Parse header fields until the empty CRLF line.
     * Consumes lines incrementally — safe to call repeatedly
     * on a growing buffer without re-processing bytes.
     * On success: fills req.headers, req.content_length,
     *             req.chunked; advances state to PSTATE_BODY
     *             or PSTATE_COMPLETE (if no body expected).
     */
    void _parseHeaders(Buffer& buf, HttpRequest& req);

    /**
     * Read exactly req.content_length bytes into req.body.
     * On completion: advances state to PSTATE_COMPLETE.
     */
    void _parseBody(Buffer& buf, HttpRequest& req);

    /**
     * Decode a chunked-encoded body into req.body.
     * Uses req._chunk_size / _chunk_trailing / _chunk_done
     * to resume correctly across recv() boundaries.
     * On completion: advances state to PSTATE_COMPLETE.
     */
    void _parseChunked(Buffer& buf, HttpRequest& req);
};

#endif // HTTP_PARSER_HPP

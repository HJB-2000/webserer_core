// ============================================================
//  HttpRequest.hpp
//  Plain data container for one parsed HTTP request.
//  C++98 compliant.
//
//  Implementation: src/HttpRequest.cpp
//
//  Ownership chain:
//    Parser        → fills   the object
//    Processing    → reads   the object
//    CGI branch    → reads   the object
//    Connection    → owns    the object (member, not pointer)
// ============================================================
#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <map>
#include <cstddef>

// ────────────────────────────────────────────────────────────
//  ParseState
//  Tracks how far the Parser has progressed on THIS request.
//
//  IDLE         → nothing received yet / reset after response
//  REQUEST_LINE → parsing the first line  (METHOD SP path SP version)
//  HEADERS      → parsing header fields
//  BODY         → reading the body (Content-Length or chunked)
//  COMPLETE     → full request is ready for Processing
//  ERROR        → parser hit a protocol violation → 400
// ────────────────────────────────────────────────────────────
enum ParseState
{
    PSTATE_IDLE         = 0,
    PSTATE_REQUEST_LINE,
    PSTATE_HEADERS,
    PSTATE_BODY,
    PSTATE_COMPLETE,
    PSTATE_ERROR
};


// ────────────────────────────────────────────────────────────
//  HttpRequest
//
//  All fields are public — this is intentionally a plain data
//  bag. The Parser writes to it, everything else only reads.
//
//  reset() must be called between keep-alive requests so the
//  same Connection object can be reused cleanly.
// ────────────────────────────────────────────────────────────
class HttpRequest
{
public:

    // ── request-line fields ────────────────────────────────
    std::string method;
    std::string path;
    std::string query_string;
    std::string version;

    // ── header fields ──────────────────────────────────────
    // Keys are stored in lower-case (Parser normalises them).
    std::map<std::string, std::string> headers;

    // ── body ───────────────────────────────────────────────
    std::string body;

    // ── parser progress ────────────────────────────────────
    ParseState  parse_state;
    size_t      content_length;
    bool        chunked;

    // ── parser error tracking ──────────────────────────────
    // HTTP status code that caused the error (400/414/431/505).
    // Only meaningful when parse_state == PSTATE_ERROR.
    int         error_code;

    // ── chunked-body parser state ──────────────────────────
    // Persist between feed() calls — private to HttpParser.
    size_t      _chunk_size;      ///< bytes left in current chunk; 0 = read size line
    bool        _chunk_trailing;  ///< next 2 bytes are trailing CRLF
    bool        _chunk_done;      ///< trailing CRLF belongs to terminal chunk

    // ── ctor / reset ───────────────────────────────────────
    HttpRequest();
    void reset();

    // ── convenience accessors ──────────────────────────────
    std::string header(const std::string& key) const;
    bool        expectsBody()                  const;
    bool        keepAlive()                    const;
};

#endif // HTTP_REQUEST_HPP

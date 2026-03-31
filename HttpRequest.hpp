// ============================================================
//  HttpRequest.hpp
//  Plain data container for one parsed HTTP request.
//  C++98 compliant — lives entirely in this header.
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

// ────────────────────────────────────────────────────────────
//  ParseState
//  Tracks how far the Parser has progressed on THIS request.
//  Stored here so Connection can decide what to do next
//  without asking the Parser directly.
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
    PS_IDLE         = 0,
    PS_REQUEST_LINE,
    PS_HEADERS,
    PS_BODY,
    PS_COMPLETE,
    PS_ERROR
};


// ────────────────────────────────────────────────────────────
//  HttpRequest
//
//  All fields are public — this is intentionally a plain data
//  bag, not an encapsulated object.  The Parser writes to it,
//  everything else only reads.
//
//  reset() must be called between keep-alive requests so the
//  same Connection object can be reused cleanly.
// ────────────────────────────────────────────────────────────
class HttpRequest
{
public:

    // ── request-line fields ────────────────────────────────

    /**
     * HTTP method as received (upper-case).
     * e.g.  "GET"  "POST"  "DELETE"
     * Parser normalises to upper-case; Processing validates
     * against the allowed_methods list from ServerConfig.
     */
    std::string method;

    /**
     * Raw path component BEFORE query-string stripping.
     * e.g.  "/cgi-bin/script.py"  "/index.html"
     * Percent-decoding is NOT done here — that is the
     * responsibility of the Processing layer.
     */
    std::string path;

    /**
     * Query string, everything AFTER the first '?' in the URI.
     * Empty string if no '?' was present.
     * e.g.  "id=42&name=foo"
     * CGI branch sets this into the QUERY_STRING env variable.
     */
    std::string query_string;

    /**
     * HTTP version string as received.
     * Expected values: "HTTP/1.0"  "HTTP/1.1"
     * Parser stores as-is; Processing enforces version policy.
     */
    std::string version;

    // ── header fields ──────────────────────────────────────

    /**
     * All header fields, key → value.
     * Keys are stored in lower-case (Parser normalises them)
     * so lookups are always case-insensitive by construction.
     *
     * Common keys used downstream:
     *   "host"             → virtual-host routing
     *   "content-length"   → body size
     *   "transfer-encoding"→ chunked detection
     *   "connection"       → keep-alive / close
     *   "content-type"     → CGI CONTENT_TYPE env var
     */
    std::map<std::string, std::string> headers;

    // ── body ───────────────────────────────────────────────

    /**
     * Raw request body bytes.
     * Populated only for POST / PUT (or any method that sends
     * a body).  Empty string for GET / DELETE / HEAD.
     *
     * The Parser reads Content-Length and copies exactly that
     * many bytes here.  Chunked encoding is de-chunked into
     * this field before state reaches PS_COMPLETE.
     */
    std::string body;

    // ── parser progress ────────────────────────────────────

    /**
     * Current parse progress for this request.
     * Connection checks this after every read to decide
     * whether to hand off to Processing yet.
     */
    ParseState  parse_state;

    /**
     * Expected body length read from Content-Length header.
     * Set by Parser when it finishes the headers phase.
     * 0 means no body expected (or not yet known).
     */
    size_t      content_length;

    /**
     * True when the Parser detected "Transfer-Encoding: chunked".
     * Processing and body-read logic branch on this flag.
     */
    bool        chunked;

    // ── ctor / reset ───────────────────────────────────────

    /**
     * Default constructor — zero / empty initialise everything.
     * Called once when Connection is constructed.
     */
    HttpRequest()
        : parse_state(PS_IDLE)
        , content_length(0)
        , chunked(false)
    {}

    /**
     * Reset all fields back to their initial state.
     * MUST be called by Connection at the start of every new
     * request on a keep-alive connection so stale data from
     * the previous cycle cannot bleed through.
     */
    void reset()
    {
        method.clear();
        path.clear();
        query_string.clear();
        version.clear();
        headers.clear();
        body.clear();
        parse_state    = PS_IDLE;
        content_length = 0;
        chunked        = false;
    }

    // ── convenience accessors (read-only helpers) ──────────

    /**
     * Look up a header value by (already lower-cased) key.
     * Returns empty string if the header is absent.
     * Does NOT throw — safe to call from any layer.
     *
     * Usage:
     *   std::string ct = req.header("content-type");
     */
    std::string header(const std::string& key) const
    {
        std::map<std::string, std::string>::const_iterator it
            = headers.find(key);
        return (it != headers.end()) ? it->second : std::string();
    }

    /**
     * True when the request carries a body that still needs
     * to be fully read (used by Connection read loop).
     */
    bool expectsBody() const
    {
        return (content_length > 0 || chunked);
    }

    /**
     * True when the client signalled it wants to keep the
     * connection alive after this request.
     *
     * Rules:
     *   HTTP/1.1 → keep-alive by default unless "connection: close"
     *   HTTP/1.0 → close by default unless "connection: keep-alive"
     */
    bool keepAlive() const
    {
        const std::string conn = header("connection");
        if (version == "HTTP/1.1")
            return (conn != "close");
        return (conn == "keep-alive");
    }
};

#endif // HTTP_REQUEST_HPP
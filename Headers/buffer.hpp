// ============================================================
//  buffer.hpp
//  Dynamic byte buffer for HTTP read / write pipelines.
//  C++98 compliant.
//
//  Implementation: src/Buffer.cpp
//
//  Lifecycle
//  ─────────
//  Construction : Buffer buf(client_max_body_size);
//  Append data  : buf.append(ptr, n);   ← called from read loop
//  Consume data : buf.consume(n);       ← called after write / parse
//  Peek         : buf.data() / buf.size()
//  Reset        : buf.reset();          ← reuse on keep-alive
//
//  Growth policy
//  ─────────────
//  initial_cap = clamp(cmbs / 4, 8 KB, 64 KB)
//  On full     : capacity doubles until >= cmbs
//  Beyond cmbs : throws BodyLimitException → caller maps to 413
// ============================================================
#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <vector>
#include <string>
#include <stdexcept>
#include <cstddef>

// ────────────────────────────────────────────────────────────
//  BodyLimitException
//  Thrown (or caught and translated to 413) when the buffer
//  would grow beyond the configured client_max_body_size.
// ────────────────────────────────────────────────────────────
class BodyLimitException : public std::runtime_error
{
public:
    explicit BodyLimitException(const std::string& msg);
};


// ────────────────────────────────────────────────────────────
//  Buffer
// ────────────────────────────────────────────────────────────
class Buffer
{
public:

    static const size_t MIN_INITIAL = 8  * 1024;   //  8 KB
    static const size_t MAX_INITIAL = 64 * 1024;   // 64 KB

    explicit Buffer(size_t client_max_body_size);

    void        append(const char* src, size_t len);
    void        consume(size_t n);
    const char* data()    const;
    char*       data();
    size_t      size()    const;
    bool        empty()   const;
    size_t      maxSize() const;
    void        reset();
    void        setMaxSize(size_t new_max);

private:
    void _compact();
    void _ensureCapacity(size_t needed);

    size_t            _cmbs;
    std::vector<char> _storage;
    size_t            _head;
};

#endif // BUFFER_HPP

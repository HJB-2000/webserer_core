// ============================================================
//  Buffer.cpp — Buffer and BodyLimitException implementations
// ============================================================
#include "Headers/buffer.hpp"

#include <cstring>    // memcpy, memmove

// ── BodyLimitException ──────────────────────────────────────

BodyLimitException::BodyLimitException(const std::string& msg)
    : std::runtime_error(msg)
{}

// ── Buffer ──────────────────────────────────────────────────

Buffer::Buffer(size_t client_max_body_size)
    : _cmbs(client_max_body_size == 0 ? (size_t)-1 : client_max_body_size)
    , _head(0)
{
    // Buffer capacity must be sufficient for HTTP headers regardless of body size limit.
    // Use 64KB as the minimum to handle large headers, then apply body limit in append().
    size_t initial = 64 * 1024;  // 64KB minimum for headers
    if (initial > _cmbs && _cmbs > 0) initial = _cmbs;
    _storage.reserve(initial);
}

void Buffer::append(const char* src, size_t len)
{
    if (len == 0)
        return;

    if (_storage.size() - _head + len > _cmbs)
        throw BodyLimitException(
            "Buffer: client_max_body_size exceeded -> 413");

    if (_head > 0 && _head >= _storage.size() / 2)
        _compact();

    _ensureCapacity(_storage.size() + len);

    const size_t old_size = _storage.size();
    _storage.resize(old_size + len);
    std::memcpy(&_storage[old_size], src, len);
}

void Buffer::consume(size_t n)
{
    if (n > size())
        n = size();
    _head += n;

    if (_head == _storage.size())
        reset();
}

const char* Buffer::data() const
{
    if (_storage.empty()) return NULL;
    return &_storage[_head];
}

char* Buffer::data()
{
    if (_storage.empty()) return NULL;
    return &_storage[_head];
}

size_t Buffer::size() const
{
    return _storage.size() - _head;
}

bool Buffer::empty() const
{
    return size() == 0;
}

size_t Buffer::maxSize() const
{
    return _cmbs;
}

void Buffer::reset()
{
    _head = 0;

    // Use a smaller initial capacity to prevent memory from growing
    // under heavy load. 16KB is sufficient for most HTTP responses.
    size_t initial = 16 * 1024;  // 16KB minimum (reduced from 64KB)
    if (initial > _cmbs && _cmbs > 0) initial = _cmbs;

    // Swap with an empty vector to truly release memory
    // This is the only way to actually free the allocated capacity
    std::vector<char> empty;
    empty.reserve(initial);
    _storage.swap(empty);
}

void Buffer::_compact()
{
    if (_head == 0) return;
    const size_t live = size();
    if (live > 0)
        std::memmove(&_storage[0], &_storage[_head], live);
    _storage.resize(live);
    _head = 0;
}

void Buffer::_ensureCapacity(size_t needed)
{
    size_t cap = _storage.capacity();
    if (cap == 0) cap = 1;  // ADD THIS
    while (cap < needed)
    {
        cap *= 2;
        if (cap > _cmbs) { cap = _cmbs; break; }
    }
    _storage.reserve(cap);
}

void Buffer::setMaxSize(size_t new_max)  
{  
    _cmbs = new_max;  
}
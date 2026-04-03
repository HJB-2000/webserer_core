// ============================================================
//  Buffer.hpp
//  Dynamic byte buffer for HTTP read / write pipelines.
//  C++98 compliant — lives entirely in this header.
// ============================================================
#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <vector>
#include <cstring>   // memcpy
#include <stdexcept> // std::runtime_error
#include <algorithm> // std::max, std::min
#include <cstddef>   // size_t

// ────────────────────────────────────────────────────────────
//  BufferOverflowException
//  Thrown (or caught and translated to 413) when the buffer
//  would grow beyond the configured client_max_body_size.
// ────────────────────────────────────────────────────────────
class BufferOverflowException : public std::runtime_error
{
public:
    explicit BufferOverflowException(const std::string& msg)
        : std::runtime_error(msg) {}
};


// ────────────────────────────────────────────────────────────
//  Buffer
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
//  Beyond cmbs : throws BufferOverflowException  → caller maps to 413
// ────────────────────────────────────────────────────────────
class Buffer
{
public:

    // ── constants ──────────────────────────────────────────
    static const size_t MIN_INITIAL = 8  * 1024;   //  8 KB
    static const size_t MAX_INITIAL = 64 * 1024;   // 64 KB

    // ── ctor ───────────────────────────────────────────────
    /**
     * @param client_max_body_size  The hard ceiling from ServerConfig.
     *                              Growth is refused beyond this value.
     */
    explicit Buffer(size_t client_max_body_size)
        : _cmbs(client_max_body_size)
        , _head(0)
    {
        // initial capacity = clamp(cmbs/4, 8KB, 64KB)
        size_t initial = _cmbs / 4;
        if (initial < MIN_INITIAL) initial = MIN_INITIAL;
        if (initial > MAX_INITIAL) initial = MAX_INITIAL;

        _storage.reserve(initial);
    }

    // ── write side (append raw bytes) ──────────────────────
    /**
     * Append [src, src+len) into the buffer.
     * Grows capacity by doubling if needed.
     * Throws BufferOverflowException if total would exceed cmbs.
     *
     * @param src  Pointer to incoming bytes (from recv / file read).
     * @param len  Number of bytes to append.
     */
    void append(const char* src, size_t len)
    {
        if (len == 0)
            return;

        // Guard: would we exceed the hard limit?
        if (_storage.size() - _head + len > _cmbs)
            throw BufferOverflowException(
                "Buffer: client_max_body_size exceeded → 413");

        // Compact before growing: slide unconsumed bytes to front
        // so we don't waste space that was already consumed.
        if (_head > 0 && _head >= _storage.size() / 2)
            _compact();

        // Ensure capacity (doubles until it fits, stays <= cmbs)
        _ensureCapacity(_storage.size() + len);

        // Append
        const size_t old_size = _storage.size();
        _storage.resize(old_size + len);
        std::memcpy(&_storage[old_size], src, len);
    }

    // ── read side (consume parsed / sent bytes) ─────────────
    /**
     * Mark the first `n` bytes as consumed.
     * Cheap O(1): just advances the head pointer.
     * Actual memory is reclaimed lazily on the next append().
     *
     * @param n  Bytes to discard from the front.
     */
    void consume(size_t n)
    {
        if (n > size())
            n = size(); // never walk past the end
        _head += n;

        // If everything was consumed, reset fully (avoids fragmentation)
        if (_head == _storage.size())
            reset();
    }

    // ── inspection ─────────────────────────────────────────
    /**
     * Pointer to the first unconsumed byte.
     * Valid until the next append() call.
     */
    const char* data() const
    {
        if (_storage.empty()) return NULL;
        return &_storage[_head];
    }

    char* data()
    {
        if (_storage.empty()) return NULL;
        return &_storage[_head];
    }

    /** Number of unconsumed bytes currently in the buffer. */
    size_t size() const
    {
        return _storage.size() - _head;
    }

    /** True when there are no unconsumed bytes. */
    bool empty() const { return size() == 0; }

    /** The configured hard ceiling (client_max_body_size). */
    size_t maxSize() const { return _cmbs; }

    // ── lifecycle ──────────────────────────────────────────
    /**
     * Full reset: drop all data, shrink back to initial capacity.
     * Call this on keep-alive reuse to reclaim memory cleanly.
     */
    void reset()
    {
        _storage.clear();
        _head = 0;

        // Recalculate initial capacity and re-reserve
        size_t initial = _cmbs / 4;
        if (initial < MIN_INITIAL) initial = MIN_INITIAL;
        if (initial > MAX_INITIAL) initial = MAX_INITIAL;
        _storage.reserve(initial);
    }

private:

    // ── internal helpers ───────────────────────────────────

    /**
     * Compact: slide [_head, end) to [0, end-_head).
     * Called lazily when head has eaten at least half the buffer.
     */
    void _compact()
    {
        if (_head == 0) return;
        const size_t live = size();
        if (live > 0)
            std::memmove(&_storage[0], &_storage[_head], live);
        _storage.resize(live);
        _head = 0;
    }

    /**
     * Ensure the internal vector can hold `needed` bytes total.
     * Doubles capacity until it fits; never exceeds _cmbs.
     */
    void _ensureCapacity(size_t needed)
    {
        size_t cap = _storage.capacity();
        while (cap < needed)
        {
            cap *= 2;
            if (cap > _cmbs) { cap = _cmbs; break; }
        }
        _storage.reserve(cap);
    }

    // ── members ────────────────────────────────────────────
    size_t            _cmbs;     ///< client_max_body_size hard limit
    std::vector<char> _storage;  ///< backing store
    size_t            _head;     ///< index of first unconsumed byte
};

#endif // BUFFER_HPP
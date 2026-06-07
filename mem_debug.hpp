// ============================================================
//  mem_debug.hpp — Body & Buffer Memory Accumulation Debugger
//
//  Drop this header anywhere you need it. No .cpp required.
//  Usage:
//    #include "mem_debug.hpp"
//    MEM_DUMP_CONNECTION(conn);      // after each request cycle
//    MEM_DUMP_REQUEST(req);          // just the HttpRequest body
//    MEM_DUMP_BUFFER(label, buf);    // just one Buffer
// ============================================================
#ifndef MEM_DEBUG_HPP
#define MEM_DEBUG_HPP

#include <iostream>
#include <iomanip>
#include <string>
#include <ctime>
#include <sstream>
// ── ANSI colours (comment out if your terminal doesn't support them) ──
#define _MEM_RED    "\033[31m"
#define _MEM_YEL    "\033[33m"
#define _MEM_GRN    "\033[32m"
#define _MEM_CYN    "\033[36m"
#define _MEM_BOLD   "\033[1m"
#define _MEM_RST    "\033[0m"

// ── Thresholds that turn a field RED ─────────────────────────
static const size_t MEM_WARN_BODY_CAP_MB  = 5;    // req.body   capacity > 5 MB  → warn
static const size_t MEM_WARN_BUF_WASTE_MB = 5;    // Buffer     waste    > 5 MB  → warn
static const size_t MEM_CRIT_MB           = 50;   // anything   > 50 MB          → RED

// ─────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────
namespace _mem_detail {

inline std::string mb(size_t bytes)
{
    std::ostringstream os;
    if (bytes >= 1024 * 1024)
        os << std::fixed << std::setprecision(2)
           << (bytes / (1024.0 * 1024.0)) << " MB";
    else if (bytes >= 1024)
        os << std::fixed << std::setprecision(1)
           << (bytes / 1024.0) << " KB";
    else
        os << bytes << " B";
    return os.str();
}

inline const char* severity(size_t bytes)
{
    size_t m = bytes / (1024 * 1024);
    if (m >= MEM_CRIT_MB)           return _MEM_RED;
    if (m >= MEM_WARN_BODY_CAP_MB)  return _MEM_YEL;
    return _MEM_GRN;
}

// Prints one labelled row:   label | size | capacity | waste | verdict
inline void row(const char* label,
                size_t      used_size,
                size_t      capacity,
                const char* extra = "")
{
    size_t waste  = (capacity > used_size) ? (capacity - used_size) : 0;
    bool   is_crit = (waste  >= MEM_CRIT_MB           * 1024 * 1024)
                  || (capacity >= MEM_CRIT_MB          * 1024 * 1024);

    std::cerr
        << "  " << std::left << std::setw(22) << label
        << " size="  << std::right << std::setw(10) << mb(used_size)
        << "  cap="  << std::setw(10) << mb(capacity)
        << "  waste=" << severity(waste) << std::setw(10) << mb(waste) << _MEM_RST;

    if (is_crit)
        std::cerr << "  " << _MEM_RED << _MEM_BOLD << "<-- LEAK SUSPECT" << _MEM_RST;

    if (extra && extra[0])
        std::cerr << "  [" << extra << "]";

    std::cerr << "\n";
}

} // namespace _mem_detail

// ─────────────────────────────────────────────────────────────
// MEM_DUMP_BUFFER(label, buf)
//
// buf must expose:  size(), maxSize(),
//                   and _storage via a local hack — see note.
//
// Because Buffer hides _storage, we infer capacity from
// Buffer::size() (live bytes) and maxSize() (ceiling).
// If you want exact internal capacity, add a capacity() accessor
// to Buffer — strongly recommended for debugging.
// ─────────────────────────────────────────────────────────────
#define MEM_DUMP_BUFFER(label_, buf_)                               \
    do {                                                            \
        size_t _sz  = (buf_).size();                                \
        size_t _max = (buf_).maxSize();                             \
        std::cerr << "  " << std::left << std::setw(22) << (label_)\
            << " live=" << std::right << std::setw(10)             \
            << _mem_detail::mb(_sz)                                 \
            << "  limit=" << std::setw(10)                         \
            << _mem_detail::mb(_max)                                \
            << "\n";                                                \
    } while (0)

// ─────────────────────────────────────────────────────────────
// MEM_DUMP_REQUEST(req)
//
// req must be an HttpRequest with public: body (std::string),
// content_length, parse_state, method, path.
// ─────────────────────────────────────────────────────────────
#define MEM_DUMP_REQUEST(req_)                                              \
    do {                                                                    \
        size_t _bsz  = (req_).body.size();                                  \
        size_t _bcap = (req_).body.capacity();                              \
        size_t _bwaste = (_bcap > _bsz) ? (_bcap - _bsz) : 0;             \
        bool   _is_crit = _bcap >= MEM_CRIT_MB * 1024 * 1024;             \
        std::cerr                                                            \
            << "  " << std::left << std::setw(22) << "req.body"            \
            << " size="  << std::right << std::setw(10)                    \
                         << _mem_detail::mb(_bsz)                          \
            << "  cap="  << std::setw(10)                                  \
                         << _mem_detail::mb(_bcap)                         \
            << "  waste=" << _mem_detail::severity(_bwaste)                \
                          << std::setw(10) << _mem_detail::mb(_bwaste)     \
                          << _MEM_RST;                                      \
        if (_is_crit)                                                       \
            std::cerr << "  " << _MEM_RED << _MEM_BOLD                     \
                      << "<-- BODY NOT FREED" << _MEM_RST;                 \
        std::cerr << "\n";                                                  \
        std::cerr                                                            \
            << "  " << std::left << std::setw(22) << "req.content_length"  \
            << " " << _mem_detail::mb((req_).content_length) << "\n";      \
    } while (0)

// ─────────────────────────────────────────────────────────────
// MEM_DUMP_CONNECTION(conn)   ← the main macro you'll call most
//
// conn must be a Connection* or Connection& with:
//   fd(), readBuffer(), writeBuffer(), request(), state()
// ─────────────────────────────────────────────────────────────
#define MEM_DUMP_CONNECTION(conn_)                                          \
    do {                                                                    \
        time_t _now = std::time(NULL);                                      \
        char   _ts[32]; std::strftime(_ts, sizeof(_ts),                    \
                            "%H:%M:%S", std::localtime(&_now));             \
        std::cerr << _MEM_BOLD _MEM_CYN                                     \
                  << "[MEM " << _ts << "] fd=" << (conn_).fd()             \
                  << "  state=" << (conn_).state() << _MEM_RST "\n";       \
        MEM_DUMP_REQUEST((conn_).request());                                \
        MEM_DUMP_BUFFER("read_buffer",  (conn_).readBuffer());              \
        MEM_DUMP_BUFFER("write_buffer", (conn_).writeBuffer());             \
        std::cerr << "\n";                                                  \
    } while (0)

// ─────────────────────────────────────────────────────────────
// FIXES — paste these replacements into your source files
// ─────────────────────────────────────────────────────────────
//
// FIX 1: HttpRequest::reset()  [HttpRequest.cpp]
// ──────────────────────────────────────────────
// BEFORE:  body.clear();
// AFTER:
//          { std::string _empty; _empty.swap(body); }
//
// Why: std::string::clear() sets size=0 but keeps allocated heap.
// After 5x 100MB POSTs, body still holds 100MB of reserved RAM.
// swap() forces deallocation immediately.
//
// ─────────────────────────────────────────────────────────────
//
// FIX 2: Buffer::reset()  [Buffer.cpp]
// ─────────────────────────────────────
// BEFORE:
//   void Buffer::reset() {
//       _storage.clear();
//       _head = 0;
//       size_t initial = 64 * 1024;
//       if (initial > _cmbs && _cmbs > 0) initial = _cmbs;
//       _storage.reserve(initial);  // ← no-op! reserve never shrinks
//   }
//
// AFTER:
//   void Buffer::reset() {
//       { std::vector<char> _empty; _empty.swap(_storage); }
//       _head = 0;
//       size_t initial = 64 * 1024;
//       if (initial > _cmbs && _cmbs > 0) initial = _cmbs;
//       _storage.reserve(initial);  // now actually reserves 64KB
//   }
//
// Why: Same problem as above. _storage.clear() keeps 100MB of
// vector capacity. The subsequent reserve(64KB) call is ignored
// because reserve() never shrinks below current capacity.
//
// ─────────────────────────────────────────────────────────────
//
// HOW TO ADD Buffer::capacity() ACCESSOR (optional but useful)
// ─────────────────────────────────────────────────────────────
// Add to Buffer.hpp:
//   size_t capacity() const { return _storage.capacity(); }
// Then MEM_DUMP_BUFFER will show exact internal capacity, not
// just the live size vs. limit.

#endif // MEM_DEBUG_HPP

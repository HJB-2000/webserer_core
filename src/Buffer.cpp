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
    : _cmbs(client_max_body_size)
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
    _storage.clear();
    _head = 0;

    // Same logic as constructor: ensure buffer can hold headers
    size_t initial = 64 * 1024;  // 64KB minimum for headers
    if (initial > _cmbs && _cmbs > 0) initial = _cmbs;
    _storage.reserve(initial);
}
void Buffer::earase()
{
    std::vector<char> empty;
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

#include <iostream>
#include <iomanip>
#include <cctype>
void Buffer::debug_dump() const {
    // 1. Calculate relative metrics based on vector state
    size_t active_data_size = _storage.size() - _head;
    size_t uncompacted_waste = _head;
    size_t vector_capacity = _storage.capacity();

    std::cout << "==================================================\n";
    std::cout << "               BUFFER METADATA DUMP               \n";
    std::cout << "==================================================\n";
    std::cout << "Client Max Body Size (_cmbs): " << _cmbs << " bytes\n";
    std::cout << "Vector Element Count (size):  " << _storage.size() << " bytes\n";
    std::cout << "Vector Total Capacity:         " << vector_capacity << " bytes\n";
    std::cout << "Read Head Cursor (_head):     " << _head << "\n";
    std::cout << "Active Readable Data Size:    " << active_data_size << " bytes\n";
    std::cout << "Uncompacted Waste Space:      " << uncompacted_waste << " bytes\n";
    std::cout << "--------------------------------------------------\n";
    std::cout << "               STORAGE DATA MEMORY MAP            \n";
    std::cout << "--------------------------------------------------\n";

    if (_storage.empty()) {
        std::cout << "[ Empty Storage Vector ]\n";
        std::cout << "==================================================\n\n";
        return;
    }

    // 2. Print out a hex & ASCII view of the raw vector memory
    // This allows you to inspect what is in the "waste" zone vs "active" zone.
    for (size_t i = 0; i < _storage.size(); ++i) {
        // Tag individual bytes for extreme clarity
        if (i == _head) {
            std::cout << " [HEAD->]";
        }

        // Print Index Label every 8 bytes for readability
        if (i % 8 == 0) {
            std::cout << "\n[" << std::setw(4) << std::setfill('0') << i << "] ";
        }

        unsigned char byte = static_cast<unsigned char>(_storage[i]);
        
        // Print Hexadecimal value
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";

        // Print character representation if printable, otherwise a dot
        std::cout << "(";
        if (std::isprint(byte)) {
            std::cout << _storage[i];
        } else {
            std::cout << ".";
        }
        std::cout << ")  ";
    }
    
    std::cout << std::dec << "\n"; // Reset stream back to decimal
    std::cout << "==================================================\n\n";
}
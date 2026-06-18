#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <vector>
#include <string>
#include <stdexcept>
#include <cstddef>

#define LimitRequestBody 20971520

class BodyLimitException : public std::runtime_error
{
public:
    explicit BodyLimitException(const std::string& msg);
};


class Buffer
{
public:

    static const size_t MIN_INITIAL = 8  * 1024;
    static const size_t MAX_INITIAL = 64 * 1024;

    explicit Buffer(size_t client_max_body_size);

    void        append(const char* src, size_t len);
    void        append_result(const char* src, size_t len);
    void        consume(size_t n);
    const char* data()    const;
    char*       data();
    size_t      size()    const;
    bool        empty()   const;
    size_t      maxSize() const;
    void        reset();
    void        setMaxSize(size_t new_max);
    void        debug_dump() const;
    void        earase();
private:
    void _compact();
    void _ensureCapacity(size_t needed);

    size_t            _cmbs;
    std::vector<char> _storage;
    size_t            _head;
};

#endif

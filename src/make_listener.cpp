#include "Headers/EventLoop.hpp"
#include "netinet/tcp.h"
#include "httpConfig.hpp"

int readSomaxconn()
{
    int fd = ::open("/proc/sys/net/core/somaxconn", O_RDONLY);
    if (fd < 0)
        return SOMAXCONN;
    char buf[16];
    std::memset(buf, 0, sizeof(buf));
    ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    for (size_t i = 0; i < sizeof(buf) && buf[i] != '\0'; ++i)
    {
        if (buf[i] < '0' || buf[i] > '9')
        {
            buf[i] = '\0';
            break;
        }
    }
    long val = 0;
    if (!safe_strtol(buf, val))
        return SOMAXCONN;
    return (val > 0) ? static_cast<int>(val) : SOMAXCONN;
}

int make_listener(const char* host, int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::cerr << "[core] socket() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    int reuse = 1;
    int reuseport = 1;
    int nodaly = 1;
    int timeout_seconds = 30;

    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        std::cerr << "[core] setsockopt SO_REUSEADDR warning: "
                  << std::strerror(errno) << "\n";
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuseport, sizeof(reuseport)) < 0) 
        std::cerr << "[core] setsockopt SO_REUSEPORT failed: " << std::strerror(errno) << "\n";

    if (::setsockopt(fd, IPPROTO_IP, TCP_NODELAY, &nodaly, sizeof(nodaly)))
        std::cerr << "[core] setsockopt  warning: "
                  << std::strerror(errno) << "\n";
    if (::setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &timeout_seconds, sizeof(timeout_seconds)) < 0)
        std::cerr << "[core] setsockopt TCP_DEFER_ACCEPT failed: "
                  << std::strerror(errno) << "\n";
    int buf_size = 1024 * 1024;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
        std::cerr << "[core] setsockopt SO_RCVBUF warning\n";
    }
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
        std::cerr << "[core] setsockopt SO_SNDBUF warning\n";
    }
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));

    if (host == NULL || std::string(host) == "0.0.0.0")
    {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        struct addrinfo hints, *res;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        int ret = ::getaddrinfo(host, NULL, &hints, &res);
        if (ret != 0)
        {
            std::cerr << "[core] getaddrinfo() failed for host " << host
                      << ": " << ::gai_strerror(ret) << "\n";
            ::close(fd);
            return -1;
        }
        addr.sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    }

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "[core] bind() failed on port " << port
                  << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    int backlog = readSomaxconn();
    if (::listen(fd, backlog) < 0)
    {
        std::cerr << "[core] listen() failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    std::cerr << "[core] listen backlog set to " << backlog << "\n";

    if (EventLoop::setNonBlocking(fd) < 0)
    {
        std::cerr << "[core] setNonBlocking() failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    std::cerr << "[core] listening on " << (host ? host : "0.0.0.0")
              << ":" << port << " (fd " << fd << ")\n";
    return fd;
}
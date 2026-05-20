#include "Headers/EventLoop.hpp"
#include "serverConfig.hpp"
#include "Headers/API_conf.hpp"
#include "Headers/Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <netdb.h> //getaddrinfo, freeaddrinfo, gai_strerror, and struct addrinfo
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

static EventLoop* g_loop = NULL;
static void sig_handler(int)
{
    if (g_loop)
        g_loop->stop();
}
// old has forbbiden func inet_addr()
// static int make_listener(const char* host, int port)
// {
//     int fd = ::socket(AF_INET, SOCK_STREAM, 0);
//     if (fd < 0)
//     {
//         std::cerr << "[core] socket() failed: " << std::strerror(errno) << "\n";
//         return -1;
//     }

//     int reuse = 1;
//     if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
//         std::cerr << "[core] setsockopt SO_REUSEADDR warning: "
//                   << std::strerror(errno) << "\n";

//     struct sockaddr_in addr;
//     std::memset(&addr, 0, sizeof(addr));
//     addr.sin_family      = AF_INET;
//     addr.sin_port        = htons(static_cast<uint16_t>(port));
//     addr.sin_addr.s_addr = (host == NULL || std::string(host) == "0.0.0.0")
//                                ? INADDR_ANY
//                                : ::inet_addr(host);

//     if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
//     {
//         std::cerr << "[core] bind() failed on port " << port
//                   << ": " << std::strerror(errno) << "\n";
//         ::close(fd);
//         return -1;
//     }

//     int backlog = readSomaxconn();
//     if (::listen(fd, backlog) < 0)
//     {
//         std::cerr << "[core] listen() failed: " << std::strerror(errno) << "\n";
//         ::close(fd);
//         return -1;
//     }
//     std::cerr << "[core] listen backlog set to " << backlog << "\n";

//     if (EventLoop::setNonBlocking(fd) < 0)
//     {
//         std::cerr << "[core] setNonBlocking() failed: " << std::strerror(errno) << "\n";
//         ::close(fd);
//         return -1;
//     }

//     std::cerr << "[core] listening on " << (host ? host : "0.0.0.0")
//               << ":" << port << " (fd " << fd << ")\n";
//     return fd;
// }


//new replacement
static int make_listener(const char* host, int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::cerr << "[core] socket() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        std::cerr << "[core] setsockopt SO_REUSEADDR warning: "
                  << std::strerror(errno) << "\n";

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
int main(int argc, char* argv[])
{
    Logger::instance().open("webserv.log");
    std::vector<ServerConfig> servers;
    try {    
        servers = API_conf(argc, argv);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "|" << e.what() << "|" << std::endl;
        return 1;
    }
        
    try {
        EventLoop            loop;
        std::vector<int>     listen_fds;

        for (size_t i = 0; i < servers.size(); ++i)
        {
            int fd = make_listener(servers[i].getHost().c_str(), servers[i].getPort());
            if (fd < 0)
            {
                std::cerr << "[core] fatal: could not create listener\n";
                return 1;
            }
            listen_fds.push_back(fd);
            try { 
                loop.addServerSocket(fd, &servers[i]);
            }
            catch(const std::exception& ex) { 
                std::cerr << "[core] fatal: failed to register server socket: " << ex.what() << "\n";  
                ::close(fd);  
                listen_fds.pop_back();  
                Logger::instance().close();  
                return 1;  
            }
        }

        g_loop = &loop;
        std::signal(SIGINT,  sig_handler);
        std::signal(SIGTERM, sig_handler);
        std::signal(SIGPIPE, SIG_IGN);
        std::cerr << "[core] server ready — press Ctrl+C to stop\n";
        loop.run();
        for (size_t i = 0; i < listen_fds.size(); ++i)
            ::close(listen_fds[i]);
        std::cerr << "[core] shutdown complete\n";
        Logger::instance().close();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[core] fatal error: " << ex.what() << "\n";  
        Logger::instance().close();  
        return 1;
    }
}

// ============================================================
//  main.cpp  —  Server core entry point
//
//  This file is the "core" for this phase.  It:
//    1. Creates a listening socket on a hardcoded port
//    2. Builds an EventLoop
//    3. Registers the socket with the loop
//    4. Installs signal handlers for clean shutdown
//    5. Runs until SIGINT / SIGTERM
//
//  Phase integration points (marked TODO):
//  ─────────────────────────────────────────────────────────
//  Phase 1 — ServerConfig / ConfigParser
//    Replace the single default_config and hardcoded port with:
//      std::vector<ServerConfig> configs = ConfigParser::parse(argv[1]);
//      for each config → bind socket → loop.addServerSocket(fd, &config)
//
//  Phase 2 — HttpParser
//    Add HttpParser as a member of EventLoop.
//    Replace _stubParse() in EventLoop.hpp with:
//      _parser.feed(conn->readBuffer(), conn->request());
//
//  Phase 3 — ResponseHandler
//    Add ResponseHandler as a member of EventLoop.
//    Replace _stubBuildResponse() / _stubSend400() with:
//      _responder.handle(conn->request(), *conn->config(), conn->writeBuffer());
//      _responder.sendError(code, *conn->config(), conn->writeBuffer());
//
//  Phase 4 — CgiHandler
//    CgiHandler is constructed inside ResponseHandler when a CGI
//    location is matched — no changes needed in main.cpp.
// ============================================================

#include "Headers/EventLoop.hpp"
#include "Headers/ServerConfig.hpp"
#include "Headers/tmpconf.hpp"
#include "Headers/Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>

// ── Signal handling ──────────────────────────────────────────
//
// A global pointer to the EventLoop lets the signal handler call
// stop() without complex IPC.  Safe because the server is
// single-threaded and g_loop is set before signal() is called.
static EventLoop* g_loop = NULL;

static void sig_handler(int /*signo*/)
{
    if (g_loop)
        g_loop->stop();
}

// ── Bind a listening socket ──────────────────────────────────
//
// Returns a bound, listening, non-blocking fd on success.
// Returns -1 on failure (error already printed to stderr).
static int make_listener(const char* host, int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::cerr << "[core] socket() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    // Allow immediate rebind after server restart (avoids "Address already in use").
    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        std::cerr << "[core] setsockopt SO_REUSEADDR warning: "
                  << std::strerror(errno) << "\n";

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = (host == NULL || std::string(host) == "0.0.0.0")
                               ? INADDR_ANY
                               : ::inet_addr(host);

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "[core] bind() failed on port " << port
                  << ": " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 128) < 0)
    {
        std::cerr << "[core] listen() failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    // Must be non-blocking before epoll ADD.
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


// ── main ─────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // ── Logging ───────────────────────────────────────────────
    // All std::cerr output from this point on is mirrored to
    // webserv.log with a UTC timestamp prefix on every line.
    Logger::instance().open("webserv.log");

    // ── TODO(phase-1): replace with ConfigParser ─────────────
    // std::vector<ServerConfig> configs = ConfigParser::parse(argv[1]);
    // Current stub: one server on port 8080 with default limits.
    ServerConfig default_config;  // values from tmpconf.hpp until Phase 1 lands
    const int    PORT = TMP_PORT;
    // ─────────────────────────────────────────────────────────

    // Build the listener socket.
    int listen_fd = make_listener(TMP_HOST, PORT);
    if (listen_fd < 0)
    {
        std::cerr << "[core] fatal: could not create listener\n";
        return 1;
    }

    // Build the event loop (creates epoll instance internally).
    EventLoop loop;

    // Register the listener with the loop.
    // ── TODO(phase-1): loop per config ───────────────────────
    // for (size_t i = 0; i < configs.size(); ++i)
    //     loop.addServerSocket(make_listener(...), &configs[i]);
    loop.addServerSocket(listen_fd, &default_config);
    // ─────────────────────────────────────────────────────────

    // Signal handling: SIGINT / SIGTERM → clean shutdown.
    g_loop = &loop;
    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    std::cerr << "[core] server ready — press Ctrl+C to stop\n";

    // Blocking event loop.
    // Returns when stop() is called (via signal handler).
    loop.run();

    // Cleanup: EventLoop destructor closes the epoll fd and all connections.
    // close the listener (not owned by EventLoop).
    ::close(listen_fd);

    std::cerr << "[core] shutdown complete\n";

    // Flush and close the log file before exit.
    Logger::instance().close();
    return 0;
}

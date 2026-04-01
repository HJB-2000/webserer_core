#include "webserver.hpp"
#include <sstream>

// Constructor initializes all descriptors to invalid values.
// Real resources are acquired later in create_socket().
socket_connection::socket_::socket_():
node(NULL), service("3490"), res(NULL), p(NULL), getaddrinfo_result(-1),
already(1), epoll_fd(-1), clients(), listener_fds(), socket_fd(-1), Hostname()
{
    std::cout << "\033[32m" << "the constructer of the socket connection is called"
     << "\033[0m" << std::endl;
    memset(&this->hints, 0, sizeof(this->hints));
    memset(this->Hostname, 0, sizeof(this->Hostname));
    gethostname(this->Hostname, HOST_NAME_MAX);
}

socket_connection::socket_::~socket_(){
    // Full cleanup order:
    // 1) close all tracked clients
    // 2) close listening socket
    // 3) close epoll fd
    // 4) free addrinfo memory if still allocated
    for (std::map<int, Connection*>::iterator it = this->clients.begin(); it != this->clients.end(); ++it)
    {
        delete it->second;
    }
    this->clients.clear();

    for (std::vector<int>::iterator it = this->listener_fds.begin(); it != this->listener_fds.end(); ++it)
    {
        if (*it >= 0)
            close(*it);
    }
    this->listener_fds.clear();
    this->socket_fd = -1;
    if (this->epoll_fd >= 0)
    {
        close(this->epoll_fd);
        this->epoll_fd = -1;
    }

    if (this->res)
    {
        freeaddrinfo(this->res);
        this->res = NULL;
    }

    std::cout << "\033[32m" << "the destructor of the socket connection is called"
    << "\033[0m" << std::endl;
}

void socket_connection::socket_::set_addrinfo_()
{
    if (this->getaddrinfo_result >= 0)
    {
        std::cerr << "\033[31mthe getinfo is not set up correctly \033[0m" << std::endl; 
        return;
    }
    this->getaddrinfo_result = getaddrinfo(this->node, this->service, &this->hints, &this->res); 
    std::cerr << "\033[31m" << this->getaddrinfo_result << "\033[0m" << std::endl;
}

int socket_connection::socket_::get_addrinfo_()
{
    return this->getaddrinfo_result;
}

void socket_connection::socket_::set_hints()
{
    // Accept both IPv4 and IPv6; TCP stream socket; server-side bind target.
    this->hints.ai_family = AF_UNSPEC;
    this->hints.ai_socktype = SOCK_STREAM;
    this->hints.ai_flags = AI_PASSIVE; 
    this->set_addrinfo_();
    if (this->getaddrinfo_result != 0)
    {
        std::cerr << "\033[31mgetaddrinfo : " << this->getaddrinfo_result << " "<< gai_strerror(this->getaddrinfo_result)<< "\033[0m" <<std::endl; 
        return ;
    }
}

int socket_connection::socket_::create_socket()
{
    // Resolve candidates and try creating up to two listeners:
    // one IPv4 and one IPv6, when available.
    this->setup();
    bool have_ipv4_listener = false;
    bool have_ipv6_listener = false;

    for(this->p = this->res; this->p != NULL; this->p = this->p->ai_next) {
        if (this->p->ai_family != AF_INET && this->p->ai_family != AF_INET6)
            continue;

        if (this->p->ai_family == AF_INET && have_ipv4_listener)
            continue;
        if (this->p->ai_family == AF_INET6 && have_ipv6_listener)
            continue;

        void *addr;
        char *ipver;
        struct sockaddr_in *ipv4;
        struct sockaddr_in6 *ipv6;
        if (this->p->ai_family == AF_INET) {
            ipv4 = (struct sockaddr_in *)this->p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = (char *)"IPv4";
        } else {
            ipv6 = (struct sockaddr_in6 *)this->p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = (char *)"IPv6";
        }
        char ipstr[INET6_ADDRSTRLEN];
        memset(ipstr, 0, sizeof(ipstr));
        inet_ntop(this->p->ai_family, addr, ipstr, sizeof(ipstr));
        std::cout << ipver << " :" << ipstr << std::endl;

        int listener_fd = socket(this->p->ai_family, this->p->ai_socktype, this->p->ai_protocol);
        if (listener_fd < 0)
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
            continue;
        }

        if (this->p->ai_family == AF_INET6)
        {
            int v6only = 1;
            (void)setsockopt(listener_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
        }

        if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &this->already,
                sizeof(int)) == -1) {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
            close(listener_fd);
            continue;
        }

        if (bind(listener_fd, this->p->ai_addr, this->p->ai_addrlen))
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
            close(listener_fd);
            continue;

        }

        if (this->set_non_blocking(listener_fd) < 0)
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
            close(listener_fd);
            continue;
        }

        if (listen(listener_fd, BACKLOG))
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
            close(listener_fd);
            continue;
        }

        this->listener_fds.push_back(listener_fd);
        if (this->socket_fd < 0)
            this->socket_fd = listener_fd;

        if (this->p->ai_family == AF_INET)
            have_ipv4_listener = true;
        else if (this->p->ai_family == AF_INET6)
            have_ipv6_listener = true;
    }

    if (this->listener_fds.empty())
        return -1;

    freeaddrinfo(this->res);
    this->res = NULL;

    // One epoll instance drives all I/O activity.
    this->epoll_fd = epoll_create(1);
    if (this->epoll_fd < 0)
    {
        std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
        return -1;
    }

    for (std::vector<int>::iterator it = this->listener_fds.begin(); it != this->listener_fds.end(); ++it)
    {
        if (this->add_fd_to_epoll(*it, EPOLLIN | EPOLLET) < 0)
            return -1;
    }

    return 0;
}

void socket_connection::socket_::setup()
{
    std::cout << "\033[32m" << this->Hostname << "\033[0m" << std::endl;
    this->set_hints();
}

int socket_connection::socket_::set_non_blocking(int fd)
{
    // Preserve existing flags and add O_NONBLOCK.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    return 0;
}

int socket_connection::socket_::add_fd_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        std::cerr << "\033[031mepoll_ctl ADD failed: " << strerror(errno) << "\033[0m" << std::endl;
        return -1;
    }
    return 0;
}

int socket_connection::socket_::remove_fd_from_epoll(int fd)
{
    // ENOENT/EBADF can happen on already-closed fds; treat as safe no-op.
    if (epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0)
    {
        if (errno == ENOENT || errno == EBADF)
            return 0;
        std::cerr << "\033[031mepoll_ctl DEL failed: " << strerror(errno) << "\033[0m" << std::endl;
        return -1;
    }
    return 0;
}

bool socket_connection::socket_::is_client_fd(int fd) const
{
    return this->clients.find(fd) != this->clients.end();
}

bool socket_connection::socket_::is_listener_fd(int fd) const
{
    for (std::vector<int>::const_iterator it = this->listener_fds.begin(); it != this->listener_fds.end(); ++it)
    {
        if (*it == fd)
            return true;
    }
    return false;
}

int socket_connection::socket_::rearm_client_events(int fd)
{
    std::map<int, Connection*>::iterator it = this->clients.find(fd);
    if (it == this->clients.end())
        return -1;

    epoll_event ev = it->second->buildEpollEvent();
    if (epoll_ctl(this->epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0)
    {
        std::cerr << "\033[031mepoll_ctl MOD failed: " << strerror(errno) << "\033[0m" << std::endl;
        return -1;
    }
    return 0;
}

void socket_connection::socket_::close_client(int fd)
{
    // Single client close path to keep cleanup behavior consistent.
    this->remove_fd_from_epoll(fd);
    std::map<int, Connection*>::iterator it = this->clients.find(fd);
    if (it != this->clients.end())
    {
        delete it->second;
        this->clients.erase(it);
    }
}

int socket_connection::socket_::accept_all_pending(int listener_fd, const ServerConfig* config)
{
    // EPOLLET requires draining accept() until EAGAIN/EWOULDBLOCK.
    const ServerConfig* effective_config = config;
    if (effective_config == NULL)
    {
        // Guard path: config wiring is not connected yet in core-only phase.
        // Keep a NULL config pointer safely until parser/config integration.
        effective_config = NULL;
    }

    while (true)
    {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof(their_addr);
        int client_socket_fd = accept(listener_fd, (struct sockaddr *)&their_addr, &addr_size);
        if (client_socket_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
            return -1;
        }

        if (this->set_non_blocking(client_socket_fd) < 0)
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
            close(client_socket_fd);
            continue;
        }

        // ============================================================
        // CONNECTION OBJECT CREATION ENTRY POINT
        // This is the exact location where one accepted client fd becomes
        // one Connection object managed by the core.
        //
        // From this point, recv/send handling goes through this object:
        // - read path  : read_from_client(fd) -> conn->recv()
        // - write path : write_to_client(fd)  -> conn->send()
        //
        // Parser/response integration will also start from this object.
        // ============================================================
        Connection* conn = new Connection(client_socket_fd, effective_config);

        epoll_event ev = conn->buildEpollEvent();
        if (epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, client_socket_fd, &ev) < 0)
        {
            delete conn;
            continue;
        }

        this->clients[client_socket_fd] = conn;

        char ipstr[INET6_ADDRSTRLEN];
        memset(ipstr, 0, sizeof(ipstr));
        void* addrptr = NULL;
        if (their_addr.ss_family == AF_INET)
            addrptr = &(((struct sockaddr_in*)&their_addr)->sin_addr);
        else
            addrptr = &(((struct sockaddr_in6*)&their_addr)->sin6_addr);

        inet_ntop(their_addr.ss_family, addrptr, ipstr, sizeof(ipstr));
        std::cout << "\033[032mserver got connection from " << ipstr
                  << " fd=" << client_socket_fd << "\033[0m" << std::endl;
    }
}

bool socket_connection::socket_::request_complete(const std::string& data) const
{
    return data.find("\r\n\r\n") != std::string::npos;
}

bool socket_connection::socket_::should_keep_alive(const std::string& request) const
{
    if (request.find("HTTP/1.0") != std::string::npos)
    {
        if (request.find("Connection: keep-alive") != std::string::npos ||
            request.find("Connection: Keep-Alive") != std::string::npos)
            return true;
        return false;
    }

    if (request.find("Connection: close") != std::string::npos ||
        request.find("Connection: Close") != std::string::npos)
        return false;
    return true;
}

void socket_connection::socket_::queue_simple_response(
    int fd, const std::string& status, const std::string& body, bool keep_alive)
{
    std::map<int, Connection*>::iterator it = this->clients.find(fd);
    if (it == this->clients.end())
        return;

    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Content-Type: text/plain\r\n"
             << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n"
             << "\r\n"
             << body;

    it->second->write_buffer = response.str();
    it->second->write_offset = 0;
    it->second->keep_alive = keep_alive;
    it->second->state = CS_WRITING;
    this->rearm_client_events(fd);
}

void socket_connection::socket_::process_client_buffer(int fd)
{
    // TODO(phase-http-parser): ENTRY POINT
    // Future flow at this location:
    // 1) Feed Connection.read_buffer to HttpParser
    // 2) Update request parse state (incomplete/complete/error)
    // 3) On complete request, call ResponseHandler builder
    // Current core-only flow below keeps networking testable.
    std::map<int, Connection*>::iterator it = this->clients.find(fd);
    if (it == this->clients.end())
        return;
    if (!this->request_complete(it->second->read_buffer))
        return;

    it->second->state = CS_PROCESSING;

    // TODO(phase-response-handler): ENTRY POINT
    // Replace this temporary response with real response generation:
    // ResponseHandler(request, config, connection) -> write_buffer
    const bool keep_alive = this->should_keep_alive(it->second->read_buffer);
    std::string body = "core ready\n";
    this->queue_simple_response(fd, "200 OK", body, keep_alive);
    it = this->clients.find(fd);
    if (it != this->clients.end())
        it->second->read_buffer.clear();
}

int socket_connection::socket_::read_from_client(int fd)
{
    // EPOLLET requires draining recv() until EAGAIN/EWOULDBLOCK.
    // Parser integration starts when process_client_buffer() is replaced
    // by HttpParser + ResponseHandler plumbing.
    std::map<int, Connection*>::iterator it = this->clients.find(fd);
    if (it == this->clients.end())
        return -1;

    Connection* conn = it->second;
    while (true)
    {
        ssize_t bytes_read = conn->recv();
        if (bytes_read > 0)
        {
            if (conn->read_buffer.size() > MAX_CLIENT_BUFFER_SIZE)
            {
                this->queue_simple_response(fd, "413 Payload Too Large", "payload too large\n", false);
                return 0;
            }
            continue;
        }
        if (bytes_read == 0)
        {
            this->process_client_buffer(fd);
            this->close_client(fd);
            return 0;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            this->process_client_buffer(fd);
            return 0;
        }
        std::cerr << "\033[032mrecv failed: " << std::strerror(errno) << "\033[0m" << std::endl;
        this->close_client(fd);
        return -1;
    }
}

int socket_connection::socket_::write_to_client(int fd)
{
    std::map<int, Connection*>::iterator it = this->clients.find(fd);
    if (it == this->clients.end())
        return -1;

    Connection* conn = it->second;
    while (conn->write_offset < conn->write_buffer.size())
    {
        ssize_t sent = conn->send();

        if (sent > 0)
            continue;

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;

        std::cerr << "\033[032msend failed: " << std::strerror(errno) << "\033[0m" << std::endl;
        this->close_client(fd);
        return -1;
    }

    if (conn->keep_alive)
    {
        conn->write_buffer.clear();
        conn->write_offset = 0;
        conn->state = CS_READING;
        this->rearm_client_events(fd);
    }
    else
    {
        conn->state = CS_CLOSING;
        this->close_client(fd);
    }
    return 0;
}

void socket_connection::socket_::sweep_idle_clients()
{
    const std::time_t now = std::time(NULL);
    std::vector<int> to_close;

    for (std::map<int, Connection*>::iterator it = this->clients.begin(); it != this->clients.end(); ++it)
    {
        if ((now - it->second->last_active) > CLIENT_IDLE_TIMEOUT_SEC)
            to_close.push_back(it->first);
    }

    for (std::vector<int>::iterator it = to_close.begin(); it != to_close.end(); ++it)
        this->close_client(*it);
}

int socket_connection::socket_::accept_connection()
{
    // One event-loop tick: wait, dispatch listener events, dispatch client events.
    if (this->epoll_fd < 0 || this->listener_fds.empty())
        return -1;

    struct epoll_event events[MAX_EVENTS];
    int ready = epoll_wait(this->epoll_fd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT_MS);
    if (ready < 0)
    {
        if (errno == EINTR)
            return 0;
        std::cerr << "\033[031mepoll_wait failed: " << strerror(errno) << "\033[0m" << std::endl;
        return -1;
    }

    for (int i = 0; i < ready; ++i)
    {
        const int fd = events[i].data.fd;
        const uint32_t ev = events[i].events;

        if (this->is_listener_fd(fd))
        {
            if (this->accept_all_pending(fd, NULL) < 0)
                return -1;
            continue;
        }

        if (!this->is_client_fd(fd))
            continue;

        if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        {
            this->close_client(fd);
            continue;
        }

        if (ev & EPOLLIN)
            this->read_from_client(fd);

        if (this->is_client_fd(fd) && (ev & EPOLLOUT))
            this->write_to_client(fd);
    }

    this->sweep_idle_clients();

    return 0;
}

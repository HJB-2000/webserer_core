#include "webserver.hpp"

socket_connection::socket_::socket_(): addrinfo_(-1),
node(NULL), service("3490"), p(NULL), res(NULL), addr_size(sizeof(their_addr)),
Hostname(), already(1)
{
    std::cout << "\033[32m" << "the constructer of the socket connection is called"
     << "\033[0m" << std::endl;
    memset(&this->hints, 0, sizeof(this->hints));
    memset(this->Hostname, 0, sizeof(this->Hostname));
    gethostname(this->Hostname, HOST_NAME_MAX);
}

socket_connection::socket_::~socket_(){
    std::cout << "\033[32m" << "the destructor of the socket connection is called"
    << "\033[0m" << std::endl;
}

void socket_connection::socket_::set_addrinfo_()
{
    if (this->addrinfo_ >= 0)
    {
        std::cerr << "\033[31mthe getinfo is not set up correctly \033[0m" << std::endl; 
        return;
    }
    this->addrinfo_ = getaddrinfo(this->node, this->service, &this->hints, &this->res); 
    std::cerr << "\033[31m" << this->addrinfo_ << "\033[0m" << std::endl;
}

int socket_connection::socket_::get_addrinfo_()
{
    return this->addrinfo_;
}

void socket_connection::socket_::set_hints()
{
    /*USE IPV4 OR IPV6 and use TCP and fill the ip address for me*/
    this->hints.ai_family = AF_UNSPEC;
    this->hints.ai_socktype = SOCK_STREAM;
    this->hints.ai_flags = AI_PASSIVE; 
    this->set_addrinfo_();
    if (this->addrinfo_ != 0)
    {
        std::cerr << "\033[31mgetaddrinfo : " << this->addrinfo_ << " "<< gai_strerror(this->addrinfo_)<< "\033[0m" <<std::endl; 
        return ;
    }
}

int socket_connection::socket_::create_socket()
{
    this->setup();
    for(this->p = this->res; this->p != NULL; this->p = this->p->ai_next) {
        if ((this->socket_fd = socket(this->res->ai_family, this->res->ai_socktype, this->res->ai_protocol)) < 0)
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl;
            continue;
        }
        if (setsockopt(this->socket_fd, SOL_SOCKET, SO_REUSEADDR, &this->already,
                sizeof(int)) == -1) {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
            exit(1);
        }
        if (bind(this->socket_fd, this->res->ai_addr, this->res->ai_addrlen))
        {
            std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
            continue;

        }
        break;
    }
    freeaddrinfo(this->res);
    if (listen(this->socket_fd, BACKLOG))
    {
        std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
        exit(1);
    }
    return 0;
}

void socket_connection::socket_::setup()
{
    std::cout << "\033[32m" << this->Hostname << "\033[0m" << std::endl;
    this->set_hints();
    for (this->p = this->res ; this->p != NULL; p = p->ai_next)
    {
        void *addr;
        char *ipver;
        struct sockaddr_in *ipv4;
        struct sockaddr_in6 *ipv6;
        if (p->ai_family == AF_INET) {
            ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = (char *)"IPv4";
        } else {
            ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = (char *)"IPv6";
        }
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        std::cout << ipver << " :" << ipstr << std::endl;       
    }
}

void* socket_connection::socket_::get_addrptr(struct sockaddr * sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int socket_connection::socket_::who_are_you() {
    int status;
    if ((status = getpeername(this->socket_fd, (struct sockaddr *)&their_addr, &this->addr_size)) < 0)
    {
        std::cerr << "\033[031mgetpeername fail \033[0m" <<std::endl; 
        return -1;
    }
    return status;
}

int socket_connection::socket_::accept_connection() {

    this->client_socket_fd = accept(this->socket_fd, (struct sockaddr *)&their_addr, &this->addr_size);
    if (this->client_socket_fd < 0)
    {
        std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
        return -1;
    }
    inet_ntop(this->their_addr.ss_family,
            this->get_addrptr((struct sockaddr *)&their_addr),
            this->ipstr, sizeof(this->ipstr));
        std::cerr << "\033[032m" << std::strerror(errno) << "\033[0m" << std::endl; 
        return -1;
    std::cout << "\033[032mserver get connection from "
        << this->ipstr << "\033[0m" <<std::endl;
    return 0;
}

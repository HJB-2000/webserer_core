#include "webserver.hpp"

socket_connection::socket_::socket_(): addrinfo_(-1),
node("http://localhost"), service("http")
{
    std::cout << "the constructer of the socket connection is called" << std::endl;
    memset(&this->hints, 0, sizeof(this->hints));
}

socket_connection::socket_::~socket_(){
    std::cout << "the destructor of the socket connection is called" << std::endl;
    freeaddrinfo(this->res);
}

void socket_connection::socket_::set_addrinfo_(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    if (this->addrinfo_ >= 0)
    {
        std::cerr << "\033[31mthe getinfo is not set up correctly \033[0m" << std::endl; 
        return;
    }
    this->addrinfo_ = getaddrinfo(node, service, hints, res);   
}

int socket_connection::socket_::get_addrinfo_()
{
    return this->addrinfo_;
}

void socket_connection::socket_::set_hints()
{
    this->hints.ai_family = AF_UNSPEC;
    this->hints.ai_socktype = SOCK_STREAM;
    this->set_addrinfo_(this->node, this->service, &this->hints, &this->res);
    if (this->addrinfo_ != 0)
    {
        std::cerr << "\033[31mgetaddrinfo :  \033[0m" << gai_strerror(this->addrinfo_) <<std::endl; 
        return ;
    }
}

void socket_connection::socket_::get_pointer_address()
{
    
    for (this->p = this->res ; this->p != NULL; p->ai_next)
    {
        void *addr;
        char *ipver;
        struct sockaddr_in *ipv4;
        struct sockaddr_in6 *ipv6;
         if (p->ai_family == AF_INET) { // IPv4
            ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } else { // IPv6
            ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        std::cout << ipver << " :" << ipstr << std::endl;       
    }
}
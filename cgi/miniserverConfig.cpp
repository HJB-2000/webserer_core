#include "miniserverConfig.hpp"

Server::Server(const std::string& host, int port, const std::string& root, int timeout)
    : _host(host), _port(port), _root(root), _timeout_seconds(timeout)
{
}

const std::string& Server::getHost() const
{
    return _host;
}

int Server::getPort() const
{
    return _port;
}

const std::string& Server::getRoot() const
{
    return _root;
}

int Server::get_timeout_seconds() const
{
    return _timeout_seconds;
}

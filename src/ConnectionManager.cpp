#include "serverConfig.hpp"
#include "Headers/ConnectionManager.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <ctime>
#include <sstream>
#include "Headers/Logger.hpp"
#include <arpa/inet.h>


size_t ConnectionManager::count() const { return _connections.size(); }
bool   ConnectionManager::empty() const { return _connections.empty(); }

ConnectionManager::ConnectionManager(int epoll_fd)
    : _epoll_fd(epoll_fd)
    , _max_connections(std::max(readSomaxconn(), 10000))
{
    std::cerr << "[ConnectionManager] max connections: " << _max_connections << "\n";
}

ConnectionManager::~ConnectionManager()
{
    _destroyAll();
}

int ConnectionManager::addConnection(int server_fd, const ServerConfig* config)
{
    static int counter = 0;
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if (static_cast<int>(_connections.size()) >= _max_connections)
        return -1;

    int client_fd = ::accept(server_fd,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &addr_len);
    if (client_fd < 0)
    {
        std::cerr << "[ConnectionManager] accept() failed: "
                  << std::strerror(errno) << "\n";
        return -1;
    }
    if (_setNonBlocking(client_fd) < 0)
    {
        std::cerr << "[ConnectionManager] setNonBlocking failed for fd "
                  << client_fd << ": " << std::strerror(errno) << "\n";
        ::close(client_fd);
        return -1;
    }
    Connection* conn = new Connection(client_fd, (ServerConfig *)config);
    conn->conn_num =  counter;
    counter++;
    if (_connections.count(client_fd))
    {
        std::cerr << "[ConnectionManager] fd " << client_fd
                  << " already tracked — closing old connection\n";
        _destroy(client_fd);
    }
    _connections[client_fd] = conn;
    std::cerr << "[ConnectionManager] accepted fd " << client_fd << "\n";
    return client_fd;
}

void ConnectionManager::closeConnection(int fd)
{
    if (!_connections.count(fd))
    {
        std::cerr << "[ConnectionManager] closeConnection(" << fd
                  << ") — fd not in map\n";
        return;
    }
    if(_connections[fd]->request().opened_file > 0)
    {
        ::close(_connections[fd]->request().opened_file);
        std::remove(_connections[fd]->request().tmp_body_path.c_str());
    }
    ::epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    _destroy(fd);
    std::cerr << "[ConnectionManager] closed fd " << fd << "\n";
}

Connection* ConnectionManager::get(int fd)
{
    std::map<int, Connection*>::iterator it = _connections.find(fd);
    return (it != _connections.end()) ? it->second : NULL;
}

std::vector<int> ConnectionManager::getTimedOutFds(time_t default_timeout_seconds)
{
    std::vector<int> stale;

    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        Connection*         conn = it->second;
        const ServerConfig* cfg  = conn->config();
        if (conn->state() == CSTATE_CGI_RUNNING)
            continue;
        time_t limit = (cfg != NULL)
            ? static_cast<time_t>(cfg->get_timeout_seconds())
            : default_timeout_seconds;
        if (conn->isTimedOut(limit))
            stale.push_back(it->first);
    }
    return stale;
}

int ConnectionManager::_setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void ConnectionManager::_destroy(int fd)
{
    delete _connections[fd];
    _connections.erase(fd);
}

void ConnectionManager::_destroyAll()
{
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        delete it->second;
    }
    _connections.clear();
}

void ConnectionManager::hold_cofiguration(std::vector<const ServerConfig*> confs, int fd)
{
    _connections[fd]->hold_cofiguration(confs);
}

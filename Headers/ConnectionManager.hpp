#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <map>
#include <vector>
#include <ctime>
#include <cstddef>

#include "serverConfig.hpp"
#include "Connection.hpp"


class ConnectionManager
{
public:

    explicit ConnectionManager(int epoll_fd);
    ~ConnectionManager();

    int addConnection(int server_fd, const ServerConfig* config);

    void closeConnection(int fd);

    void rearmEpoll(int fd);

    Connection*       get(int fd);
    const Connection* get(int fd) const;

    std::vector<int> getTimedOutFds(time_t default_timeout_seconds = 60);

    size_t count() const;
    bool   empty() const;

private:

    ConnectionManager(const ConnectionManager&);
    ConnectionManager& operator=(const ConnectionManager&);

    static int _setNonBlocking(int fd);
    void       _destroy(int fd);
    void       _destroyAll();

    int                        _epoll_fd;
    int                        _max_connections;
    std::map<int, Connection*> _connections;
};

#endif
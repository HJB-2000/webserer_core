#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>

class Server 
{
    public:
        Server(const std::string& host, int port, const std::string& root, int timeout);

        const std::string& getHost() const;
        int getPort() const;
        const std::string& getRoot() const;
        int get_timeout_seconds() const;

    private:
        std::string _host;
        int _port;
        std::string _root;
        int _timeout_seconds;
};

#endif

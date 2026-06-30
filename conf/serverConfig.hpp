#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

class Location;
class httpConfig;
#include <string>
#include <map>
#include <vector>
#include "locationConfig.hpp"
#include "httpConfig.hpp"
#include <iostream>
#include <stdexcept>
class Server
{
    public :
        Server();
        Server(const Server& obj);
        Server(const httpConfig& obj_http);
        Server& operator=(const Server& obj);
        ~Server();

        std::string getRoot() const;
        size_t getMaxBody() const;
        std::map<int, std::string> getErrorPageMap() const;
        std::string getHost() const;
        int getPort() const;
        std::vector<std::string> getIndex_s() const;
        std::string getServerName() const;
        std::vector<Location>& getLocations();
        const std::vector<Location>& get_locations() const;
        const Location* matchLocation(const std::string& path) const;
        int get_timeout_seconds() const;

        void setHost(const std::string& host);
        void setPort(int &port);
        void setServerName(const std::string& name);
        void setRoot(std::string& root);
        void setIndex_s(const std::string& index_s);
        void setMaxBodySize(long long size);
        void setErrorPage(int code, std::string& path);
        void addLocation(Location& loc);
        void set_default_conf();
        void set_timeout_seconds(int time_out);
    private :
        std::string _host;
        int _port;
        int _timeout_seconds;
        std::string _root;
        long long _client_max_body_size;
        std::vector<std::string> _index_Files;
        std::string _server_name;
        std::map<int, std::string> _error_page;
        std::vector<Location> _locations;

};

typedef Server ServerConfig;

#endif

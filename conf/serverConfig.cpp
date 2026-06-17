#include "serverConfig.hpp"
// #include <string>
// #include <iostream>
// #include <cstdlib>
// #include <stdexcept>
// will check if this a dead code  
const Server* matchServer(const std::vector<Server>& servers, const std::string& host_header, int port)
{
    std::string host_only = host_header;
    size_t colon_pos = host_only.find(':');
    if (colon_pos != std::string::npos)
        host_only = host_only.substr(0, colon_pos);
    for (size_t i = 0; i < servers.size(); ++i)
    {
        if (servers[i].getPort() == port)
        {
            const std::string& server_name = servers[i].getServerName();
            // for (size_t j = 0; j < names.size(); ++j)
            // {
            //     if (names[j] == host_only)
            //         return &servers[i];
            // }
            if(server_name == host_only)
            {
                // std::cerr << "!!!!!" +  server_name + "!!!!!"<< std::endl;
                return &servers[i];
            }
        }
    }
    for (size_t i = 0; i < servers.size(); ++i)
    {
        if (servers[i].getPort() == port)
            return &servers[i];
    }
    return NULL;
}

const Location* Server::matchLocation(const std::string& path) const
{
    const Location* best = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < _locations.size(); ++i)
    {
        const std::string& loc_path = _locations[i].getPath();
        if (loc_path.empty())
            continue;

        if (path.compare(0, loc_path.size(), loc_path) != 0)
            continue;

        if (!loc_path.empty() && loc_path[loc_path.size() - 1] != '/')
        {
            if (path.size() > loc_path.size() && path[loc_path.size()] != '/')
                continue;
        }

        if (loc_path.length() > best_len)
        {
            best = &_locations[i];
            best_len = loc_path.length();
        }
    }
    return best;
}
Server::Server() :
    _host(""),
    _port(-1),
    _timeout_seconds(0),
    _root(""),
    _client_max_body_size(0)
{
}

Server::Server(const Server& obj)
{
    this->_host = obj._host;
    this->_port = obj._port;
    this->_timeout_seconds = obj._timeout_seconds;
    this->_root = obj._root;
    this->_index_Files = obj._index_Files;
    this->_server_name = obj._server_name;
    this->_client_max_body_size = obj._client_max_body_size;
    this->_error_page = obj._error_page;
    this->_locations = obj._locations;
}

Server::Server(const httpConfig& obj_http)
{
    this->_client_max_body_size = obj_http.get_cl_mx_bd_sz();
    this->_error_page = obj_http.get_error_page();
    this->_host = "";
    this->_port = -1;
    this->_timeout_seconds = 0;
    this->_root = "";
    this->_server_name = "";
    this->_locations.clear();
}

Server& Server::operator=(const Server& obj)
{
    if(this != &obj)
    {
        this->_host = obj._host;
        this->_port = obj._port;
        this->_timeout_seconds = obj._timeout_seconds;
        this->_root = obj._root;
        this->_client_max_body_size = obj._client_max_body_size;
        this->_index_Files = obj._index_Files;
        this->_server_name = obj._server_name;
        this->_error_page = obj._error_page;
        this->_locations = obj._locations;
    }
    return (*this);
}

Server::~Server()
{
}

std::string Server::getRoot() const         { return _root; }
size_t      Server::getMaxBody() const      { return static_cast<size_t>(this->_client_max_body_size); }
std::string Server::getHost() const         { return _host; }
int         Server::getPort() const         { return _port; }
int         Server::get_timeout_seconds() const { return _timeout_seconds; }

std::map<int, std::string> Server::getErrorPageMap() const { return _error_page; }
std::vector<std::string>   Server::getIndex_s() const      { return _index_Files; }
std::string   Server::getServerName() const  { return _server_name; }
const std::vector<Location>& Server::get_locations() const { return _locations; }
std::vector<Location>& Server::getLocations() { return _locations; }
void Server::setHost(const std::string& host)        { this->_host = host; }
void Server::setRoot(std::string& root)              { this->_root = root; }
void Server::setIndex_s(const std::string& index_s)  { this->_index_Files.push_back(index_s); }
void Server::setMaxBodySize(long long size)
{
    this->_client_max_body_size = size;
}
void Server::addLocation(Location& loc)              { this->_locations.push_back(loc); }
void Server::set_timeout_seconds(int time_out)       { this->_timeout_seconds = time_out; }

void Server::setPort(int &port)
{
    // if(_port == -1)
    this->_port = port;
    // else
    // {
    //     std::cerr << "|" << this->_port << "|" << std::endl;
    //     std::cerr << "Duplicate port in the directive listen" << std::endl;
    //     throw std::runtime_error("Duplicate port in the directive listen");
    // }
}

void Server::setServerName(const std::string& name)
{
    this->_server_name = name;
}

void Server::setErrorPage(int code, std::string& path)
{
    this->_error_page[code] = path;
}

void Server::set_default_conf()
{
    _host = "127.0.0.1";
    _port = 8080;
    _root = "./www/html";
    _client_max_body_size = 10485760;
    _index_Files.push_back("index.html");
    _server_name = "example.com";
    _error_page[400] = "./errors/400.html";
    _error_page[500] = "./errors/500.html";
    Location locations_block;
    locations_block.set_default_conf(0);
    this->_locations.push_back(locations_block);
    locations_block.set_default_conf(1);
    this->_locations.push_back(locations_block);
    locations_block.set_default_conf(2);
    this->_locations.push_back(locations_block);
    locations_block.set_default_conf(3);
    this->_locations.push_back(locations_block);
}

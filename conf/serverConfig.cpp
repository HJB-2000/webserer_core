#include "serverConfig.hpp"

const Location* Server::matchLocation(const std::string& path) const
{
    const Location* best = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < _locations.size(); ++i)
    {
        std::string loc_path = _locations[i].getPath();
        if (loc_path.empty())
            continue;

        if (loc_path.size() > 1 && loc_path[loc_path.size() - 1] == '/')
            loc_path.erase(loc_path.size() - 1);

        std::string req_path = path;
        if (req_path.size() > 1 && req_path[req_path.size() - 1] == '/')
            req_path.erase(req_path.size() - 1);

        if (req_path == loc_path || 
            (req_path.compare(0, loc_path.size(), loc_path) == 0 && req_path[loc_path.size()] == '/'))
        {
            if (_locations[i].getPath().length() > best_len)
            {
                best = &_locations[i];
                best_len = _locations[i].getPath().length();
            }
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

void Server::setPort(int &port) { this->_port = port; }

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
    _host                 = "localhost";
    _port                 = 8080;
    _timeout_seconds      = 60;
    _root                 = "./www/html";
    _client_max_body_size = 2040109465LL;
    _index_Files.push_back("index.html");
    _server_name          = "example.com";

    _error_page[400] = "./www/html/errors/400.html";
    _error_page[401] = "./www/html/errors/401.html";
    _error_page[403] = "./www/html/errors/403.html";
    _error_page[404] = "./www/html/errors/404.html";
    _error_page[405] = "./www/html/errors/405.html";
    _error_page[408] = "./www/html/errors/408.html";
    _error_page[411] = "./www/html/errors/411.html";
    _error_page[413] = "./www/html/errors/413.html";
    _error_page[500] = "./www/html/errors/500.html";
    _error_page[502] = "./www/html/errors/502.html";
    _error_page[503] = "./www/html/errors/503.html";
    _error_page[504] = "./www/html/errors/504.html";

    Location loc;
    loc.set_default_conf(0); this->_locations.push_back(loc);
    loc.set_default_conf(1); this->_locations.push_back(loc);
    loc.set_default_conf(2); this->_locations.push_back(loc);
    loc.set_default_conf(3); this->_locations.push_back(loc);
    loc.set_default_conf(4); this->_locations.push_back(loc);
    loc.set_default_conf(5); this->_locations.push_back(loc);
}
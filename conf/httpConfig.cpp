#include "httpConfig.hpp"
#include <string>
#include <iostream>
#include <cstdlib>

httpConfig::httpConfig() : _client_max_body_size(0), _exist_location(false)
{
}
httpConfig::httpConfig(const httpConfig& obj)
{
    this->_client_max_body_size = obj._client_max_body_size;
    this->_error_page = obj._error_page;
    this->_all_servers = obj._all_servers;
    this->_exist_location = obj._exist_location;
}

httpConfig& httpConfig::operator=(const httpConfig& obj)
{
    if(this != &obj)
    {
        this->_client_max_body_size = obj._client_max_body_size;
        this->_error_page = obj._error_page;
        this->_all_servers = obj._all_servers;
        this->_exist_location = obj._exist_location;
    }
    return (*this);
}

httpConfig::~httpConfig()
{
}

void httpConfig::set_cl_mx_bd_sz(long long cl_mx_bd_sz)
{
    this->_client_max_body_size = cl_mx_bd_sz;
}
long long httpConfig::get_cl_mx_bd_sz() const
{
    return _client_max_body_size;
}
void httpConfig::set_error_page(int err_code, std::string err_path)
{
    this->_error_page[err_code] = err_path;
}
std::map<int, std::string> httpConfig::get_error_page() const
{
    return _error_page;
}

void httpConfig::set_servers(Server& server)
{
    this->_all_servers.push_back(server);
}

const std::vector<Server>& httpConfig::get_all_servers() const
{
    return _all_servers;
}

std::vector<std::string> httpConfig::consumeValues(size_t &i, std::vector<Lexer> &stream)
{
    std::vector<std::string> values;
    i++;
    while (i < stream.size() && stream[i].get_token_type() == "TYPE_VALUE")
    {
        values.push_back(stream[i].get_value());
        i++;
    }
    if (i < stream.size() && stream[i].get_token_type() == "TYPE_SEMICOLON")
    {
        i++;
    }
    else
    {
        report_parse_error("Expected semicolone after the value", stream, i, "in consumeValues");
    }
    return values;
}

std::vector<std::string> httpConfig::consumeDirective(size_t &j, std::vector<Lexer> &stream)
{
    std::vector<std::string> directives;
    j++;
    while (j < stream.size() && stream[j].get_token_type() == "TYPE_DIRECTIVE")
    {
        directives.push_back(stream[j].get_value());
        j++;
    }
    return directives;
}

void httpConfig::set_default_conf()
{
    _client_max_body_size = 10485760;
    _error_page[404] = "./errors/404.html";
    _error_page[500] = "./errors/500.html";
    Server servers_block;
    servers_block.set_default_conf();
    _all_servers.push_back(servers_block);
    _exist_location = false;
}

void httpConfig::set_exist_location()
{
    this->_exist_location = true;
}
bool httpConfig::get_exist_location()
{
    return _exist_location;
}

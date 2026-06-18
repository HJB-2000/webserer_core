#include "locationConfig.hpp"
#include "serverConfig.hpp"

Location::Location() :  _path(""),
                        _root(""),
                        _autoindex(false),
                        _cgi_paths(),
                        _cgi_extensions(),
                        _cgi_map(),
                        _upload(""),
                        _return_code(-1),
                        _return_value(""),
                        _client_max_body_size(0),
                        _redirect_enabled(false)
{
}
Location::Location(const Location& obj)
{
    this->_path = obj._path;
    this->_allowed_methods = obj._allowed_methods;
    this->_root = obj._root;
    this->_index_Files = obj._index_Files;
    this->_autoindex = obj._autoindex;
    this->_cgi_paths = obj._cgi_paths;
    this->_cgi_extensions = obj._cgi_extensions;
    this->_cgi_map = obj._cgi_map;
    this->_upload = obj._upload;
    this->_return_code = obj._return_code;
    this->_return_value = obj._return_value;
    this->_redirect_enabled = obj._redirect_enabled;
    this->_client_max_body_size = obj._client_max_body_size;
    this->_error_page = obj._error_page;
}

Location::Location(const Server& obj_server)
{
    this->_root = obj_server.getRoot();
    this->_index_Files = obj_server.getIndex_s();
    this->_client_max_body_size = obj_server.getMaxBody();
    this->_error_page = obj_server.getErrorPageMap();

    this->_path = "";
    this->_autoindex = false;
    this->_cgi_paths.clear();
    this->_cgi_extensions.clear();
    this->_cgi_map.clear();
    this->_upload = "";
    this->_return_code = -1;
    this->_return_value = "";
    this->_redirect_enabled = false;
    this->_allowed_methods.clear();
}

Location& Location::operator=(const Location& obj)
{
    if(this != &obj)
    {
        this->_path = obj._path;
        this->_allowed_methods = obj._allowed_methods;
        this->_root = obj._root;
        this->_index_Files = obj._index_Files;
        this->_autoindex = obj._autoindex;
        this->_cgi_paths = obj._cgi_paths;
        this->_cgi_extensions = obj._cgi_extensions;
        this->_cgi_map = obj._cgi_map;
        this->_upload = obj._upload;
        this->_return_code = obj._return_code;
        this->_return_value = obj._return_value;
        this->_redirect_enabled = obj._redirect_enabled;
        this->_client_max_body_size = obj._client_max_body_size;
        this->_error_page = obj._error_page;
    }
    return (*this);
}

Location::~Location()
{
}

void Location::setPath(std::string path)           { this->_path = path; }
void Location::setMethods(std::string method)      { this->_allowed_methods.push_back(method); }
void Location::setRoot(const std::string& root)    { this->_root = root; }
void Location::setIndex_s(const std::string& idx)  { this->_index_Files.push_back(idx); }
void Location::setCGI_extensions(const std::vector<std::string>& extensions)
{
    this->_cgi_extensions = extensions;
    this->_cgi_map.clear();
    if (this->_cgi_paths.size() == this->_cgi_extensions.size())
    {
        for (size_t i = 0; i < this->_cgi_extensions.size(); ++i)
            this->_cgi_map[this->_cgi_extensions[i]] = this->_cgi_paths[i];
    }
}
void Location::setCGI_paths(const std::vector<std::string>& paths)
{
    this->_cgi_paths = paths;
    this->_cgi_map.clear();
    if (this->_cgi_paths.size() == this->_cgi_extensions.size())
    {
        for (size_t i = 0; i < this->_cgi_extensions.size(); ++i)
            this->_cgi_map[this->_cgi_extensions[i]] = this->_cgi_paths[i];
    }
}

void Location::setUploadStore(const std::string& upload) { this->_upload = upload; }
void Location::setClientMaxBodySize(long long size)
{
    this->_client_max_body_size = size;
}

void Location::setAutoindex(const std::string& autoindex)
{
    if(autoindex == "on")        this->_autoindex = true;
    else if(autoindex == "off")  this->_autoindex = false;
}

void Location::setReturnRedirection(int code, std::string path)
{
    _return_code = code;
    _return_value = path;
    _redirect_enabled = (code != -1);
}

void Location::set_error_page_loc(int err_code, std::string err_path)
{
    this->_error_page[err_code] = err_path;
}

std::string              Location::getPath() const                 { return _path; }
std::vector<std::string> Location::getMethods() const             { return _allowed_methods; }
std::string              Location::getRoot() const                 { return _root; }
std::vector<std::string> Location::getIndex_s() const             { return _index_Files; }
bool                     Location::getAutoindex() const            { return _autoindex; }
const std::map<std::string, std::string>& Location::getCGI_map() const { return _cgi_map; }
bool                     Location::hasPartialCGIConfig() const
{
    if (_cgi_paths.empty() && _cgi_extensions.empty())
        return false;
    return _cgi_paths.size() != _cgi_extensions.size()
        || _cgi_map.size() != _cgi_paths.size();
}
std::string              Location::getUploadStore() const          { return _upload; }
size_t                   Location::getClientMaxBodySize() const    { return static_cast<size_t>(this->_client_max_body_size); }
int                      Location::getReturnRedirection_code() const   { return _return_code; }
std::string              Location::getReturnRedirection_path() const   { return _return_value; }
bool                     Location::getRedirectEnabled() const          { return _redirect_enabled; }
std::map<int, std::string> Location::get_error_page_loc() const       { return _error_page; }

void Location::clear_index() { _index_Files.clear(); }

void Location::set_default_conf(int num)
{
    if(num == 0)
    {
        if(!_index_Files.empty()) 
            _index_Files.clear();
        _allowed_methods.clear();
        this->_path = "/";
        this->_root = "./www/html";
        this->_index_Files.push_back("index.html");
        this->_autoindex = false;
        this->_allowed_methods.push_back("GET");
        this->_allowed_methods.push_back("POST");
        this->_cgi_paths.clear();
        this->_cgi_extensions.clear();
        this->_cgi_map.clear();
        this->_upload = "";
        this->_return_code = -1;
        this->_return_value = "";
        this->_client_max_body_size = 10485760;
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
    }
    else if(num == 1)
    {
        if(!_index_Files.empty()) 
            _index_Files.clear();
        _allowed_methods.clear();
        this->_path = "/uploads/";
        this->_root = "./www/html";
        this->_index_Files.push_back("index.html");
        this->_autoindex = true;
        this->_allowed_methods.push_back("GET");
        this->_allowed_methods.push_back("POST");
        this->_allowed_methods.push_back("DELETE");
        this->_cgi_paths.clear();
        this->_cgi_extensions.clear();
        this->_cgi_map.clear();
        this->_upload = "./www/html/uploads/";
        this->_return_code = -1;
        this->_return_value = "";
        this->_client_max_body_size = 20485760;
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
    }
    else if(num == 2)
    {
        if(!_index_Files.empty()) 
            _index_Files.clear();
        _allowed_methods.clear();
        this->_path = "/dashboard/";
        this->_root = "./www/html";
        this->_index_Files.push_back("index.html");
        this->_autoindex = false;
        this->_allowed_methods.push_back("GET");
        this->_cgi_paths.clear();
        this->_cgi_extensions.clear();
        this->_cgi_map.clear();
        this->_upload = "";
        this->_return_code = -1;
        this->_return_value = "";
        this->_redirect_enabled = false;
        this->_client_max_body_size = 10485760;
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
    }
    else if(num == 3)
    {
        if(!_index_Files.empty())
            _index_Files.clear();
        _allowed_methods.clear();
        this->_path = "/cgi-bin/";
        this->_root = "./www/html";
        this->_index_Files.push_back("index.html");
        this->_autoindex = false;
        this->_allowed_methods.push_back("GET");
        this->_allowed_methods.push_back("POST");
        this->_cgi_paths.clear();
        this->_cgi_paths.push_back("/usr/bin/python3");
        this->_cgi_extensions.clear();
        this->_cgi_extensions.push_back(".py");
        this->_cgi_map.clear();
        this->_cgi_map[".py"] = "/usr/bin/python3";
        this->_upload = "";
        this->_return_code = -1;
        this->_return_value = "";
        this->_client_max_body_size = 10485760;
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
    }
    else if(num == 4)
    {
        if(!_index_Files.empty())
            _index_Files.clear();
        _allowed_methods.clear();
        this->_path = "/redirect/";
        this->_root = "./www/html";
        this->_index_Files.push_back("index.html");
        this->_autoindex = false;
        this->_allowed_methods.push_back("GET");
        this->_cgi_paths.clear();
        this->_cgi_extensions.clear();
        this->_cgi_map.clear();
        this->_upload = "";
        this->_return_code = 301;
        this->_return_value = "/new-page.html";
        this->_client_max_body_size = 10485760;
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
    }
}
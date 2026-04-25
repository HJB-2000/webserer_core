❌✅
## 1. segfault based on this routine:
### Fixed: ❌ 
### - httpConfig::parseDirective
```
    else if (directive == "return")
    {
        location.setReturnRedirection(atoi(values[0].c_str()), values[1]);
    }
```
- plus the atoi problems there is no chek on atoi return value or parameters
## 2- a critical bug in the copy assingment operater of THE SERVER
### Fixed : ✅
### - all members are coppied execept of the index-files vector
```
    Server& Server::operator=(const Server& obj)
    {
        if(this != &obj)
        {
            this->_host = obj._host;
            this->_port = obj._port;
            this->_timeout_seconds = obj._timeout_seconds;
            this->_root = obj._root;
            this->_server_names = obj._server_names;
            this->_client_max_body_size = obj._client_max_body_size;
            this->_error_page = obj._error_page;
            this->_locations = obj._locations;
        }
        return (*this);
    }
```
## 3- so fucked up {this is so crazy wlllah}
### Fixed : ❌ {i dont know}
```
bool is_valid_number(const std::string str)
{
    for(size_t i = 0; i < str.length(); i++)
    {
        if(isdigit(str[i]) == 0)
            return false;
    }
    return true;
}
```
- what about empty string what is the return of this function if i did 
```
    is_valid_number("");
```
- up to you to decide

## 4- 
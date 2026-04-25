#ifndef CGI_REQUEST_INFO_HPP
#define CGI_REQUEST_INFO_HPP

#include <string>
#include "serverConfig.hpp"

struct CgiRequestInfo
{
    const Location  *location;
    std::string     script_path;
};

#endif
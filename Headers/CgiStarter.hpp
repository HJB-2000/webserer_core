#ifndef CGI_STARTER_HPP
#define CGI_STARTER_HPP

#include <string>
#include "HttpRequest.hpp"
#include "serverConfig.hpp"

bool startCgi(
    const HttpRequest& req,
    const ServerConfig& cfg,
    const Location& loc,
    const std::string& script_path,
    int result_write_fd
);

#endif
// ============================================================
//  ServerConfig.hpp  —  TEMPORARY STUB
//
//  The real ServerConfig.hpp is owned by a teammate (Phase 1).
//  This stub exists only so the project compiles while that
//  file is not yet available.
//
//  FIELDS match Plan.md exactly — the teammate must provide
//  the same field names and types.  When their file arrives,
//  delete this file and src/ServerConfig.cpp entirely and drop
//  theirs in place.  Nothing else in the codebase needs to change.
//
//  Implementation: src/ServerConfig.cpp
// ============================================================
#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <cstddef>

#include "tmpconf.hpp"

// ── Location ─────────────────────────────────────────────────
struct Location
{
    std::string              path;
    std::string              root;
    std::string              index;
    std::vector<std::string> allowed_methods;
    bool                     autoindex;
    std::string              cgi_extension;
    std::string              cgi_path;
    std::string              upload_path;
    bool                     redirect_enabled;
    int                      redirect_code;
    std::string              redirect_url;

    Location();
};

// ── ServerConfig ─────────────────────────────────────────────
struct ServerConfig
{
    std::string               host;
    int                       port;
    std::vector<std::string>  server_names;
    std::string               root;
    std::string               index;
    size_t                    client_max_body_size;
    int                       timeout_seconds;
    std::map<int,std::string> error_pages;
    std::vector<Location>     locations;

    ServerConfig();

    const Location* matchLocation(const std::string& path) const;
};

#endif // SERVER_CONFIG_HPP

// ============================================================
//  ServerConfig.hpp  —  TEMPORARY STUB
//
//  The real ServerConfig.hpp is owned by a teammate (Phase 1).
//  This stub exists only so the project compiles while that file
//  is not yet available.
//
//  FIELDS match Plan.md exactly — the teammate must provide
//  the same field names and types.  When their file arrives,
//  delete this file entirely and drop theirs in its place.
//  Nothing else in the codebase needs to change.
//
//  Values come from tmpconf.hpp macros so there is one place
//  to update them during development.
// ============================================================
#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <cstddef>

#include "tmpconf.hpp"

// ── Location (nested in ServerConfig per Plan.md) ────────────
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

    Location()
        : autoindex(TMP_AUTOINDEX)
        , redirect_enabled(TMP_REDIRECT_ENABLED)
        , redirect_code(TMP_REDIRECT_CODE)
    {}
};

// ── ServerConfig ─────────────────────────────────────────────
struct ServerConfig
{
    std::string              host;
    int                      port;
    std::vector<std::string> server_names;
    std::string              root;
    std::string              index;
    size_t                   client_max_body_size;
    int                      timeout_seconds;
    std::map<int,std::string>error_pages;
    std::vector<Location>    locations;

    ServerConfig()
        : host(TMP_HOST)
        , port(TMP_PORT)
        , root(TMP_ROOT)
        , index(TMP_INDEX)
        , client_max_body_size(TMP_CLIENT_MAX_BODY_SIZE)
        , timeout_seconds(TMP_TIMEOUT_SECONDS)
    {}

    // Longest-prefix location matching (Plan.md §2)
    // Replace with teammate's implementation when available.
    const Location* matchLocation(const std::string& path) const
    {
        const Location* best       = NULL;
        size_t          best_len   = 0;

        for (size_t i = 0; i < locations.size(); ++i)
        {
            const Location& loc = locations[i];
            if (path.compare(0, loc.path.size(), loc.path) == 0)
            {
                if (loc.path.size() > best_len)
                {
                    best     = &loc;
                    best_len = loc.path.size();
                }
            }
        }
        return best;
    }
};

#endif // SERVER_CONFIG_HPP

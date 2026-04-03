// ============================================================
//  ServerConfig.cpp — Location and ServerConfig implementations
//
//  This is the STUB implementation — replaced entirely when
//  the teammate delivers the real ServerConfig.hpp + .cpp.
// ============================================================
#include "Headers/ServerConfig.hpp"

// ── Location ─────────────────────────────────────────────────

Location::Location()
    : autoindex(TMP_AUTOINDEX)
    , redirect_enabled(TMP_REDIRECT_ENABLED)
    , redirect_code(TMP_REDIRECT_CODE)
{}

// ── ServerConfig ─────────────────────────────────────────────

ServerConfig::ServerConfig()
    : host(TMP_HOST)
    , port(TMP_PORT)
    , root(TMP_ROOT)
    , index(TMP_INDEX)
    , client_max_body_size(TMP_CLIENT_MAX_BODY_SIZE)
    , timeout_seconds(TMP_TIMEOUT_SECONDS)
{}

// ── matchLocation ────────────────────────────────────────────
//
// Longest-prefix matching — same algorithm as nginx.
// Returns the most specific Location whose path is a prefix
// of the request path, or NULL if none match.
const Location* ServerConfig::matchLocation(const std::string& path) const
{
    const Location* best     = NULL;
    size_t          best_len = 0;

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

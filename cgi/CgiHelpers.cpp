// #include "Headers/ResponseHandler.hpp"

// bool ResponseHandler::resolveCgiRequest(
//     const HttpRequest&  req,
//     const ServerConfig& cfg,
//     CgiRequestInfo&     out
// ) const
// {
//     const Location* loc = cfg.matchLocation(req.path);
//     if (!loc || loc->getCGI_extensions().empty()){ return false; }
//     const std::vector<std::string>& exts = loc->getCGI_extensions();
//     bool matched = false;
//     for (size_t ei = 0; ei < exts.size(); ++ei)
//     {
//         const std::string& ext = exts[ei];
//         if (req.path.size() >= ext.size()
//             && req.path.compare(req.path.size() - ext.size(), ext.size(), ext) == 0)
//         {
//             matched = true;
//             break;
//         }
//     }
//     if (!matched){ return false; }
//     out.location = loc;
//     out.script_path = _resolveFsPath(req, loc, cfg);
//     return true;
// }

#include "Headers/ResponseHandler.hpp"

bool ResponseHandler::resolveCgiRequest(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    CgiRequestInfo&     out
) const
{
    const Location* loc = cfg.matchLocation(req.path);
    if (!loc || loc->getCGI_map().empty()){ return false; }
    const std::map<std::string, std::string>& cgi_map = loc->getCGI_map();
    bool matched = false;
    for (std::map<std::string, std::string>::const_iterator it = cgi_map.begin(); it != cgi_map.end(); ++it)
    {
        const std::string& ext = it->first;
        if (req.path.size() >= ext.size()
            && req.path.compare(req.path.size() - ext.size(), ext.size(), ext) == 0)
        {
            matched = true;
            break;
        }
    }
    if (!matched){ return false; }
    out.location = loc;
    out.script_path = _resolveFsPath(req, loc, cfg);
    return true;
}
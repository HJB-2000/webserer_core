#include "parsing.hpp"
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

void check_valid_content(std::stringstream &buff)
{
    std::string str = buff.str();

    for (size_t i = 0; i < str.size(); i++)
    {
        unsigned char c = static_cast<unsigned char>(str[i]);

        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 126))
            continue;
        buff.clear();
        throw std::runtime_error(std::string("[API_conf] Invalid character ] in config file"));
    }
}

void remove_comments(std::stringstream &buff)
{
    std::string str = buff.str();
    char hashtag = '#';
    std::string out;
    bool in_single_quote = false;
    bool in_double_quote = false;
    for(size_t i = 0; i < str.length(); i++)
    {
        if(str[i] == '\'' && !in_double_quote)
            in_single_quote = !in_single_quote;
        else if(str[i] == '"' && !in_single_quote)
            in_double_quote = !in_double_quote;

        bool starts_token = (i == 0) || std::isspace(static_cast<unsigned char>(str[i - 1]));
        if(str[i] == hashtag && starts_token && !in_single_quote && !in_double_quote)
        {
            i++;
            for(; i < str.length(); i++)
            {
                if(str[i] == '\n')
                    break;
            }
            if(i >= str.length())
                break;
        }
        out.push_back(str[i]);
    }
    buff.str(out);
}

void refactoring_buffer(std::stringstream &buff)
{
    std::string str = buff.str();
    size_t len = str.length();
    std::string out;
    for(size_t i = 0; i < len; )
    {
        if(isspace(str[i]))
        {
            while(i < len && isspace(str[i]))
                i++;
            out.push_back(' ');
        }
        else
        {
            out.push_back(str[i]);
            i++;
        }
    }
    if(!out.empty() && out[out.size() - 1] == ' ')
        out.erase(out.size() - 1);
    if(!out.empty() && out[0] == ' ')
        out.erase(0, 1);
    buff.str(out);
}

void insert_space(std::stringstream &buff)
{
    std::string str = buff.str();
    size_t len = str.length();
    std::string out;
    for(size_t i = 0; i < len; i++)
    {
        if(str[i] == '{' || str[i] == '}' || str[i] == ';')
        {
            out.push_back(' ');
            out.push_back(str[i]);
            out.push_back(' ');
        }
        else
        {
            out.push_back(str[i]);
        }
    }
    buff.str(out);
}

std::vector<std::string> storing_in_vec(std::stringstream &buff)
{
    std::vector<std::string> tokens;
    std::string tmp;
    while(std::getline(buff, tmp, ' '))
    {
        if(!tmp.empty())
            tokens.push_back(tmp);
    }
    return tokens;
}

static std::string port_to_str(int port) 
{
    std::ostringstream oss;
    oss << port;
    return oss.str();
}

static bool is_valid_filename(const std::string& name)
{
    if (name.empty() || name == "." || name == "..")
        return false;

    if (name.find('/') != std::string::npos)
        return false;

    if (name[0] == '.')
        return false;

    if (name[name.size() - 1] == '.')
        return false;
    if (name.rfind('.') == std::string::npos)
        return false;

    return true;
}

static bool is_directory(const std::string& path) 
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
        return false;
    return (info.st_mode & S_IFDIR);
}

static bool is_regular_file(const std::string& path) 
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
        return false;
    return (info.st_mode & S_IFREG);
}

static bool is_executable(const std::string& path) 
{
    return (access(path.c_str(), X_OK) == 0);
}

static bool is_readable(const std::string& path) 
{
    return (access(path.c_str(), R_OK) == 0);
}

void validate_final_config(std::vector<Server>& servers, long long http_default_cmbs)
{
    std::set<std::pair<int, std::string> > seen_virtual_servers;
    for (size_t s = 0; s < servers.size(); ++s) 
    {
        const Server& server = servers[s];
        std::string server_id = "Server on port " + port_to_str(server.getPort());
        int tmp_port = server.getPort();
        std::string tmp_server_name = server.getServerName();
        std::pair<int, std::string> current_server = std::make_pair(tmp_port, tmp_server_name);
        if(seen_virtual_servers.find(current_server) != seen_virtual_servers.end())
                throw std::runtime_error("[fatal] " + server_id + 
                                 " : duplicate server_name '" + tmp_server_name + 
                                 "' defined on the same port.");
        seen_virtual_servers.insert(current_server);




        std::string s_root = server.getRoot();
        if (s_root.empty() || (s_root[0] != '.' && s_root[0] != '/')) 
        {
            throw std::runtime_error(std::string("[fatal] " + server_id + " : Root path must be absolute or relative (./)."));
        }
        if (!is_directory(s_root)) 
        {
            throw std::runtime_error(std::string("[fatal]" + server_id + " : Root directory not found: " + s_root));
        }

        std::map<int, std::string> s_errs = server.getErrorPageMap();
        for (std::map<int, std::string>::const_iterator it = s_errs.begin(); it != s_errs.end(); ++it) 
        {
            if (it->first < 300 || it->first > 599) 
            {
                throw std::runtime_error(std::string("[fatal]" + server_id + ": Invalid error code"));
            }
            if (!is_regular_file(it->second) || !is_readable(it->second)) 
            {
                throw std::runtime_error(std::string("[fatal]" + server_id + ": Error page file not found or unreadable: " + it->second));
            }
        }
        long long client_max_body_size_server = server.getMaxBody();
        if (client_max_body_size_server == 0)
        {
            long long fallback = (http_default_cmbs > 0) ? http_default_cmbs : 1048576;
            servers[s].setMaxBodySize(fallback);
            std::cerr << "[WARN] at [-------server--------] " << server_id
                      << ": Client_max_body_size missing, using default " << fallback << std::endl;
        }
        std::vector<Location>& locs = servers[s].getLocations();
        for (size_t l = 0; l < locs.size(); ++l) 
        {
            Location& loc = locs[l];
            std::string loc_id = server_id + " [" + loc.getPath() + "]";

            if (!is_directory(loc.getRoot())) 
            {
                throw std::runtime_error(std::string("[fatal]" + server_id + ": Location root not found: " + loc.getRoot()));
            }
            long long client_max_body_size_location = loc.getClientMaxBodySize();
            if (client_max_body_size_location == 0)
            {
                long long inherited = servers[s].getMaxBody();
                loc.setClientMaxBodySize(inherited);
                client_max_body_size_location = inherited;
                std::cerr << "[WARN] at [-------location--------] " << loc_id
                          << ": Client_max_body_size missing, inheriting " << inherited << std::endl;
            }

            std::vector<std::string> idxs = loc.getIndex_s();
            for (size_t i = 0; i < idxs.size(); ++i) 
            {
                if (!is_valid_filename(idxs[i])) 
                {
                    throw std::runtime_error(std::string("[fatal]" + loc_id + ": Invalid index filename: " + idxs[i]));
                }
            }

            if (loc.hasPartialCGIConfig())
            {
                throw std::runtime_error(std::string("[fatal]" + loc_id + ": CGI requires the same number of paths and extensions."));
            }

            const std::map<std::string, std::string>& cgi_map = loc.getCGI_map();
            for (std::map<std::string, std::string>::const_iterator it = cgi_map.begin(); it != cgi_map.end(); ++it)
            {
                std::string ext = it->first;
                std::string path = it->second;

                if (ext.empty() || path.empty())
                {
                    throw std::runtime_error(std::string("[fatal]" + loc_id + ": CGI map contains an empty extension or binary path."));
                }
                if (!is_executable(path))
                {
                    throw std::runtime_error(std::string("[fatal]" + loc_id + ": CGI binary not executable for " + ext + " : " + path));
                }
            }

            if (!loc.getUploadStore().empty()) 
            {
                std::string up = loc.getUploadStore();
                if (!is_directory(up) || access(up.c_str(), W_OK) != 0) 
                {
                    throw std::runtime_error(std::string("[fatal]" + loc_id + ": Upload directory not found or not writable: " + up));
                }
                std::vector<std::string> methods = loc.getMethods();
                if(!methods.empty())
                {
                    bool has_post = false;
                    for (size_t k = 0; k < methods.size(); k++)
                    {
                        if (methods[k] == "POST") 
                        {
                            has_post = true;
                            break;
                        }
                    }
                    if (!has_post)
                       throw std::runtime_error(std::string("[fatal]" + loc_id + ": upload_path requires POST in allowed_methods"));
                }

            }
            std::map<int, std::string> l_errs = loc.get_error_page_loc();
            for (std::map<int, std::string>::const_iterator it = l_errs.begin(); it != l_errs.end(); ++it) 
            {
                if (!is_regular_file(it->second) || !is_readable(it->second)) 
                {
                    throw std::runtime_error(std::string("[fatal]" + loc_id + ": Error page file not found: " + it->second));
                }
            }
        }
    }
}
void parsing_lexems(ParserConf& parser, std::vector<Lexer>& stream_lexems)
{
    size_t len = stream_lexems.size();
    size_t i = 0;
    while (i < len)
    {
        std::string type = stream_lexems[i].get_token_type();
        std::string val  = stream_lexems[i].get_value();

        if (type == "TYPE_CONTEXT" && val == "events")
        {
            eventsConfig events;
            parser.parseEvents(events, stream_lexems, i);
            parser.set_events(events);
        }
        else if (type == "TYPE_CONTEXT" && val == "http")
        {
            if (parser.get_exist_http())
                report_parse_error("Duplicate block: http", stream_lexems, i, "parsing_lexems");
            parser.set_exist_http();
            parser.parsHTTP(stream_lexems, i);
        }
        else if (type == "TYPE_END")
        {
            break;
        }
        else
        {
            report_parse_error("Syntax Error: unexpected token at top level", stream_lexems, i, "parsing_lexems");
        }
    }
    parser.check_for_blocks();
    if(parser.get_http().get_cl_mx_bd_sz() < 20000)
    {        parser.get_http().set_cl_mx_bd_sz(1048576);
        std::cerr << "[WARN] at [-------http--------]" << ": Client_max_body_size was not provided, Use Default 1m" << std::endl;

    }
    validate_final_config(parser.get_http().get_all_servers(),
                          parser.get_http().get_cl_mx_bd_sz());
}
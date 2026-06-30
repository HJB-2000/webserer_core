#include "httpConfig.hpp"
#include <iostream>
#include <cstdlib>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>

#include <string>
#include <cctype>
#include <cstdlib> 

static bool is_valid_octet(const std::string& s)
{
    if (s.empty() || s.length() > 3)
        return false;
    if (s[0] == '0' && s.length() > 1)
        return false;

    for (size_t i = 0; i < s.length(); ++i)
    {
        if (!isdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    long num = 0;
    if (!safe_strtol(s, num))
        return false;
    return num >= 0 && num <= 255;
}

static bool is_valid_host(const std::string& host)
{
    if (host.empty())
        return false;

    size_t prev = 0;
    int dots = 0;

    for (size_t i = 0; i < host.size(); ++i)
    {
        if (host[i] == '.')
        {
            if (!is_valid_octet(host.substr(prev, i - prev)))
                return false;
            if (++dots > 3) 
                return false;
            prev = i + 1;
        }
    }
    return is_valid_octet(host.substr(prev)) && dots == 3;
}


void verifying_path_in_locations_inside_server(Server &obj_Server)
{
    const std::vector<Location>& tmp_locations = obj_Server.get_locations();
    std::set<std::string> unique_paths;

    for (size_t i = 0; i < tmp_locations.size(); i++)
    {
        std::string path = tmp_locations[i].getPath();
        if (path.size() > 1 && path[path.size() - 1] == '/')
            path.erase(path.size() - 1);

        if (unique_paths.find(path) != unique_paths.end())
        {
            throw std::runtime_error("[fatal] duplicate pathin location : " + tmp_locations[i].getPath());
        }
        unique_paths.insert(path);
    }
}

static void validate_index_filenames(const std::vector<std::string>& index_files,
                                     std::vector<Lexer>& stream, size_t i,
                                     const char* context)
{
    for (size_t k = 0; k < index_files.size(); ++k)
    {
        const std::string& name = index_files[k];
        size_t dot_pos = name.rfind('.');
        if (dot_pos == std::string::npos || dot_pos == 0 || dot_pos == name.size() - 1)
        {
            report_parse_error(
                "Invalid index filename '" + name + "': must have a non-empty file extension (e.g. index.html)",
                stream, i, context);
        }
    }
}

void Location::check_for_allowed_methods()
{
    if(_allowed_methods.empty())
        _allowed_methods.push_back("GET");
}

void httpConfig::parseDirective(Server &server, const std::string &directive, std::vector<Lexer> &stream, size_t &i)
{
    std::vector<std::string> values = consumeValues(i, stream);

    if (values.empty())
        report_parse_error("Syntax Error: directive ", stream, i, "'has no value' in parseDirective of server");

    if (directive == "listen")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'listen expects a single value' in parseDirective of server");

        std::string val = values[0];
        size_t colon_pos = val.find(':');

        if (colon_pos != std::string::npos)
        {
            std::string tmp_host = val.substr(0, colon_pos);
            std::string tmp_port = val.substr(colon_pos + 1);
            
            if (tmp_host == "*")
            {
                tmp_host = "0.0.0.0";
                server.setHost(tmp_host);
            }
            else if (tmp_host == "localhost")
            {
                server.setHost(tmp_host);
            }
            else {
                if (!is_valid_host(tmp_host))
                    report_parse_error("Syntax Error: directive ", stream, i,
                        "'invalid host in listen directive — expected IPv4, localhost, or *' in parseDirective of server");                    
                server.setHost(tmp_host);
            }

            long port_long;
            if (!safe_strtol(tmp_port, port_long))
                report_parse_error("Syntax Error: directive ", stream, i,
                    "'not valid port' in parseDirective of server");

            int port = static_cast<int>(port_long);
            if (port < 1 || port > 65535)
                report_parse_error("Syntax Error: directive ", stream, i,
                    "'not valid range for port (1-65535)' in parseDirective of server");
            
            server.setPort(port);
        }
        else
        {
            long port_long;
            if (safe_strtol(val, port_long))
            {
                int tmp_port = static_cast<int>(port_long);
                if (tmp_port < 1 || tmp_port > 65535)
                    report_parse_error("Syntax Error: directive ", stream, i,
                        "'not valid range for port (1-65535)' in parseDirective of server");
                std::string default_host = "0.0.0.0";
                server.setHost(default_host);
                server.setPort(tmp_port);
            }
            else if (is_valid_host(val))
            {
                std::string tmp_host = val;
                server.setHost(tmp_host);
                int default_port = 9090;
                std::cerr << "[WARN] Missing port in configuration. Falling back to default (9090)." << std::endl;
                server.setPort(default_port);
            }
            else
            {
                report_parse_error("Syntax Error: directive ", stream, i,
                    "'listen value must be a port, an IPv4 address, or host:port' in parseDirective of server");
            }
        }
    }
    else if (directive == "root")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'root expects a single value' in parseDirective of server");
        server.setRoot(values[0]);
    }
    else if (directive == "index")
    {
        if (values.empty())
            report_parse_error("Syntax Error: directive", stream, i,
                "'index expects at least one value' in parseDirective of Location");
        for (size_t j = 0; j < values.size(); j++)
        {
            if (values[j].empty())
                report_parse_error("Syntax Error: directive", stream, i,
                    "'index value cannot be empty' in parseDirective of Location");
            server.setIndex_s(values[j]);

        }
    }
    else if (directive == "server_name")
    {
        if (values.empty())
            report_parse_error("Syntax Error: directive ", stream, i,
                "'server_name expects at least one value' in parseDirective of server");
        server.setServerName(values[0]);
    }
    else if (directive == "client_max_body_size")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'client_max_body_size expects a single value' in parseDirective of server");
        long long tmp = parse_cl_mx_bd_sz(values[0]);

        if (tmp == -1 || tmp > MAX_CLIENT_BODY_SIZE_LIMIT)
        {
            report_parse_error("Syntax Error", stream, i,
                "Invalid client_max_body_size");
        }
        server.setMaxBodySize(tmp);
    }
    else if (directive == "timeout")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'timeout expects a single value' in parseDirective of server");
        long tmp_long;
        if (!safe_strtol(values[0], tmp_long))
            report_parse_error("Syntax Error: directive ", stream, i,
                "'Invalid value of timeout' in parseDirective of server");

        int tmp = static_cast<int>(tmp_long);
        if (tmp <= 0 || tmp > 3600)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'Invalid range of value of timeout' in parseDirective of server");
        server.set_timeout_seconds(tmp);
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'error_page needs at least a code and a path' in parseDirective of server");
        std::string error_path = values.back();
        for (size_t j = 0; j < values.size() - 1; j++)
        {
            long code_long;
            if (!safe_strtol(values[j], code_long))
                report_parse_error("Syntax Error: directive ", stream, i,
                    "'Invalid error code' in parseDirective of server");

            int code = static_cast<int>(code_long);
            if (code < 300 || code > 599)
                report_parse_error("Syntax Error: directive ", stream, i,
                    "'Invalid error code' in parseDirective of server");
            server.setErrorPage(code, error_path);
        }
    }
    else if (directive == "allowed_methods")
    {
        report_parse_error("Syntax Error: directive ", stream, i,
            "'allowed_methods is only allowed in location context'");
    }
    else
    {
        report_parse_error("Syntax Error: directive ", stream, i,
            "'unknown server directive' in parseDirective of server");
    }
}

void httpConfig::parsServer(Server &obj_Server, std::vector<Lexer> &stream_lexems, size_t &i)
{
    i++;
    if (i >= stream_lexems.size() || stream_lexems[i].get_token_type() != "TYPE_LBRACE")
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Expected '{' after server' in parsServer");
    i++;

    std::set<std::string> seen_directives;
    std::set<std::string> unique_directives;
    unique_directives.insert("listen");
    unique_directives.insert("root");
    unique_directives.insert("client_max_body_size");
    unique_directives.insert("timeout");
    unique_directives.insert("index");

    while (i < stream_lexems.size() && stream_lexems[i].get_token_type() != "TYPE_RBRACE")
    {
        std::string val  = stream_lexems[i].get_value();
        std::string type = stream_lexems[i].get_token_type();

        if (type == "TYPE_CONTEXT" && val == "location")
        {
            Location new_loc(obj_Server);
            parsLocation(new_loc, stream_lexems, i);
            new_loc.check_for_allowed_methods();
            obj_Server.addLocation(new_loc);
        }
        else if (type == "TYPE_DIRECTIVE")
        {
            if (unique_directives.find(val) != unique_directives.end())
            {
                if (seen_directives.find(val) != seen_directives.end())
                    report_parse_error("Duplicate directive in server", stream_lexems, i, val.c_str());
                seen_directives.insert(val);
            }
            parseDirective(obj_Server, val, stream_lexems, i);
        }
        else
            report_parse_error("Syntax Error: ", stream_lexems, i, "'unexpected token in server block' in parsServer");
    }

    if (i >= stream_lexems.size() || stream_lexems[i].get_token_type() != "TYPE_RBRACE")
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Unclosed server block' in parsServer");
    if (obj_Server.getPort() == -1)
    {
        report_parse_error("Missing required directive: listen", stream_lexems, i, "parsServer");
    }
    if (obj_Server.getRoot().empty())
        report_parse_error("Missing required directive: root", stream_lexems, i, "parsServer");
    if (obj_Server.getIndex_s().empty())
        report_parse_error("Missing required directive: index", stream_lexems, i, "parsServer");
    validate_index_filenames(obj_Server.getIndex_s(), stream_lexems, i, "parsServer/index");
    if (obj_Server.get_timeout_seconds() <= 0)
        report_parse_error("Missing required directive: timeout", stream_lexems, i, "parsServer");
    std::vector<Location>& locs = obj_Server.getLocations();
    for (size_t idx = 0; idx < locs.size(); ++idx)
    {
        Location& loc = locs[idx];

        if (loc.getRoot().empty())
            loc.setRoot(obj_Server.getRoot());
        if (loc.getIndex_s().empty())
            loc.setIndex_s(obj_Server.getIndex_s().front());
        if (loc.getClientMaxBodySize() == 0)
            loc.setClientMaxBodySize(static_cast<long long>(obj_Server.getMaxBody()));
        if (loc.get_error_page_loc().empty())
        {
            std::map<int, std::string> serv_errors = obj_Server.getErrorPageMap();
            for (std::map<int, std::string>::const_iterator it = serv_errors.begin();
                it != serv_errors.end(); ++it)
            {
                loc.set_error_page_loc(it->first, it->second);
            }
        }
    }

    for (size_t idx = 0; idx < locs.size(); ++idx)
    {
        bool is_redirect = (locs[idx].getReturnRedirection_code() != -1);
        bool is_cgi      = (!locs[idx].getCGI_map().empty());
        if (!is_redirect && !is_cgi)
            validate_index_filenames(locs[idx].getIndex_s(), stream_lexems, i,
                                     ("parsServer/location[" + locs[idx].getPath() + "]/index").c_str());
    } 
    verifying_path_in_locations_inside_server(obj_Server);
    i++;
}
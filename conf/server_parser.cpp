#include "httpConfig.hpp"
#include <iostream>
#include <cstdlib>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#define MAX_SERVER_LIMIT 1073741824LL

static bool is_valid_host(std::string tmp_host)
{
    int dot = 0;
    int num = 0;
    int prev = 0;
    for(size_t i = 0; i < tmp_host.size(); i++)
    {
        if(tmp_host[i] == '.')
        {
            std::string host_str = tmp_host.substr(prev, i - prev);
            if(!is_valid_number(host_str))
                return false;
            num = atoi(host_str.c_str());
            if(num < 0 || num > 255 || dot++ >= 3)
                return false;
            prev = i + 1;
        }
    }
    num = atoi(tmp_host.substr(prev).c_str());
    return (num >= 0 && num <= 255 && dot == 3);
}

static bool is_existing_directory(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

static bool path_exists(const std::string& path)
{
    return access(path.c_str(), F_OK) == 0;
}

static bool is_executable_file(const std::string& path)
{
    return access(path.c_str(), F_OK | X_OK) == 0;
}

static void validate_filesystem_directives(const Server &obj_Server, std::vector<Lexer> &stream, size_t i)
{
    std::string root = obj_Server.getRoot();
    if (!root.empty())
    {
        if (path_exists(root))
        {
            if (!is_existing_directory(root))
                report_parse_error("Invalid filesystem directive: root is not a directory", stream, i, "validate_filesystem_directives");
        }
        else
            std::cerr << "[warn] root path does not exist at parse time: " << root << std::endl;
    }

    std::vector<Location> locs = obj_Server.get_locations();
    for (size_t k = 0; k < locs.size(); ++k)
    {
        std::string loc_root = locs[k].getRoot();
        if (!loc_root.empty() && path_exists(loc_root))
        {
            if (!is_existing_directory(loc_root))
                report_parse_error("Invalid filesystem directive: location root is not a directory", stream, i, "validate_filesystem_directives");
        }

        std::string upload = locs[k].getUploadStore();
        if (!upload.empty())
        {
            if (path_exists(upload))
            {
                if (!is_existing_directory(upload))
                    report_parse_error("Invalid filesystem directive: upload_path is not a directory", stream, i, "validate_filesystem_directives");
            }
            else
                std::cerr << "[warn] upload path does not exist at parse time: " << upload << std::endl;
        }

        std::string cgi = locs[k].getCGI_path();
        if (!cgi.empty())
        {
            if (path_exists(cgi))
            {
                if (!is_executable_file(cgi))
                    report_parse_error("Invalid filesystem directive: cgi_path is not executable", stream, i, "validate_filesystem_directives");
            }
            else
                std::cerr << "[warn] cgi_path does not exist at parse time: " << cgi << std::endl;
        }
    }
}

void verifying_path_in_locations_inside_server(Server &obj_Server)
{
    std::vector<Location> tmp_locations = obj_Server.get_locations();
    std::set<std::string> unique_paths;

    for (size_t i = 0; i < tmp_locations.size(); i++)
    {
        std::string path = tmp_locations[i].getPath();
        if (unique_paths.find(path) != unique_paths.end())
        {
            std::cerr << "duplicate path in some location context" << std::endl;
            throw std::runtime_error("duplicate path in some location context");
        }
        unique_paths.insert(path);
    }
}

static void validate_locations_of_server(Location &loc, std::vector<Lexer> &stream, size_t i)
{
    bool has_ext = !loc.getCGI_extension().empty();
    bool has_path = !loc.getCGI_path().empty();
    if (has_ext != has_path)
        report_parse_error("Invalid CGI configuration: cgi_ext and cgi_path must be used together", stream, i, "parsServer/parsLocation");

    std::string upload = loc.getUploadStore();
    if (!upload.empty())
    {
        std::vector<std::string> methods = loc.getMethods();
        bool hasPost = false;
        for (size_t k = 0; k < methods.size(); k++)
        {
            if (methods[k] == "POST") { hasPost = true; break; }
        }
        if (!hasPost)
            report_parse_error("Invalid location: upload_path requires POST in allowed_methods", stream, i, "parsServer/parsLocation");
    }

    int ret_code = loc.getReturnRedirection_code();
    if (ret_code != -1 && (ret_code < 300 || ret_code >= 400))
        report_parse_error("Invalid return code: must be 3xx for redirection", stream, i, "parsServer/parsLocation");

    if (loc.getIndex_s().empty())
        report_parse_error("Missing required directive in location: index", stream, i, "parsServer/parsLocation");
    if (loc.getRoot().empty())
        report_parse_error("Missing required directive in location: root", stream, i, "parsServer/parsLocation");
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
        std::string val = values[0];
        size_t colon_pos = val.find(':');
        if (colon_pos != std::string::npos)
        {
            std::string tmp_host = val.substr(0, colon_pos);
            std::string tmp_port = val.substr(colon_pos + 1);
            if(is_valid_host(tmp_host))
                server.setHost(val.substr(0, colon_pos));
            else
                report_parse_error("Syntax Error: directive ", stream, i, "'invalid host in listen directive' in parseDirective of server");
            if(is_valid_number(tmp_port))
            {
                int port = atoi(tmp_port.c_str());
                if(port < 1 || port > 65535)
                    report_parse_error("Syntax Error: directive ", stream, i, "'not valid range for port' in parseDirective of server");
                server.setPort(port);
            }
            else
                report_parse_error("Syntax Error: directive ", stream, i, "'not valid port' in parseDirective of server");
        }
        else
        {
            if(is_valid_number(val))
            {
                int tmp_port = atoi(val.c_str());
                if(tmp_port < 1 || tmp_port > 65535)
                    report_parse_error("Syntax Error: directive ", stream, i, "'not valid range for port' in parseDirective of server");
                server.setPort(tmp_port);
            }
            else
                report_parse_error("Syntax Error: directive ", stream, i, "'not valid port' in parseDirective of server");
        }
    }
    else if (directive == "root")
    {
        server.setRoot(values[0]);
    }
    else if (directive == "index")
    {
        for(size_t j = 0; j < values.size(); j++)
            server.setIndex_s(values[j]);
    }
    else if (directive == "server_name")
    {
        server.setServerNames(values);
    }
    else if(directive == "client_max_body_size")
    {
        long long tmp = parse_cl_mx_bd_sz(values[0]);
        if(tmp == -1 || tmp > MAX_SERVER_LIMIT)
            report_parse_error("Syntax Error", stream, i, "Invalid client_max_body_size");
        server.setMaxBodySize(tmp);
    }
    else if(directive == "timeout")
    {
        if(is_valid_number(values[0]))
        {
            int tmp = atoi(values[0].c_str());
            if(tmp < 0 || tmp > 3600)
                report_parse_error("Syntax Error: directive ", stream, i, "'Invalid range of value of timeout' in parseDirective of server");
            server.set_timeout_seconds(tmp);
        }
        else
            report_parse_error("Syntax Error: directive ", stream, i, "'Invalid value of timeout' in parseDirective of server");
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
            report_parse_error("Syntax Error: directive ", stream, i, "'error_page needs at least a code and a path' in parseDirective of server");
        std::string error_path = values.back();
        for (size_t j = 0; j < values.size() - 1; j++)
        {
            if(is_valid_number(values[j]))
            {
                int code = atoi(values[j].c_str());
                if (code < 300 || code > 599)
                    report_parse_error("Syntax Error: directive ", stream, i, "'Invalid error code' in parseDirective of server");
                server.setErrorPage(code, error_path);
            }
            else
                report_parse_error("Syntax Error: directive ", stream, i, "'Invalid error code' in parseDirective of server");
        }
    }
    else if (directive == "allowed_methods")
    {
        report_parse_error("Syntax Error: directive ", stream, i, "'allowed_methods is only allowed in location context'");
    }
    else
    {
        report_parse_error("Syntax Error: directive ", stream, i, "'unknown server directive' in parseDirective of server");
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
    unique_directives.insert("server_name");

    while (i < stream_lexems.size() && stream_lexems[i].get_token_type() != "TYPE_RBRACE")
    {
        std::string val  = stream_lexems[i].get_value();
        std::string type = stream_lexems[i].get_token_type();

        if (type == "TYPE_CONTEXT" && val == "location")
        {
            Location new_loc(obj_Server);
            parsLocation(new_loc, stream_lexems, i);
            new_loc.check_for_allowed_methods();
            validate_locations_of_server(new_loc, stream_lexems, i);
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
        report_parse_error("Missing required directive: listen", stream_lexems, i, "parsServer");
    if (obj_Server.getRoot().empty())
        report_parse_error("Missing required directive: root", stream_lexems, i, "parsServer");
    if (obj_Server.getServerNames().empty())
        report_parse_error("Missing required directive: server_name", stream_lexems, i, "parsServer");
    if (obj_Server.getIndex_s().empty())
        report_parse_error("Missing required directive: index", stream_lexems, i, "parsServer");
    if (obj_Server.get_timeout_seconds() <= 0)
        report_parse_error("Missing required directive: timeout", stream_lexems, i, "parsServer");

    validate_filesystem_directives(obj_Server, stream_lexems, i);
    verifying_path_in_locations_inside_server(obj_Server);
    i++;
}

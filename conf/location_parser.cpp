#include "httpConfig.hpp"
#include <iostream>
#include <cstdlib>
#include <set>
#include <errno.h>
#include <ctype.h>

void httpConfig::parsLocation(Location &obj_Location, std::vector<Lexer> &stream_lexems, size_t &i)
{
    if (i < stream_lexems.size() && stream_lexems[i].get_value() == "location")
        i++;

    if (i < stream_lexems.size() && stream_lexems[i].get_token_type() == "TYPE_VALUE")
    {
        std::string path = stream_lexems[i].get_value();
        if (path.empty())
            report_parse_error("Syntax Error: ", stream_lexems, i,
                "'location path cannot be empty' in parsLocation");
        if (path[0] != '/')
            report_parse_error("Syntax Error: ", stream_lexems, i,
                "'location path must start with /' in parsLocation");
        for (size_t ci = 0; ci < path.size(); ++ci)
        {
            unsigned char c = static_cast<unsigned char>(path[ci]);
            if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                continue;
            report_parse_error("Syntax Error: ", stream_lexems, i,
                "'location path contains invalid character' in parsLocation");
        }

        obj_Location.setPath(path);
        i++;
    }
    else if (i < stream_lexems.size() && stream_lexems[i].get_token_type() == "TYPE_CONTEXT")
    {
        report_parse_error("Configuration Error: ", stream_lexems, i, 
            "'location path cannot be a reserved keyword (server, location, http, events)' in parsLocation");
    }
    else
        report_parse_error("Syntax Error: ", stream_lexems, i, 
            "'Expected path after location' in parsLocation");

    if (i >= stream_lexems.size() || stream_lexems[i].get_token_type() != "TYPE_LBRACE")
        report_parse_error("Syntax Error: ", stream_lexems, i, 
            "'Expected '{' after location path' in parsLocation");
    i++;

    std::set<std::string> seen_directives;
    std::set<std::string> unique_directives;
    unique_directives.insert("allowed_methods");
    unique_directives.insert("root");
    unique_directives.insert("index");
    unique_directives.insert("autoindex");
    unique_directives.insert("cgi_path");
    unique_directives.insert("cgi_ext");
    unique_directives.insert("upload_store");
    unique_directives.insert("upload_path");
    unique_directives.insert("client_max_body_size");
    unique_directives.insert("return");

    while (i < stream_lexems.size() && stream_lexems[i].get_token_type() != "TYPE_RBRACE")
    {
        std::string type = stream_lexems[i].get_token_type();
        std::string val  = stream_lexems[i].get_value();

        if (type == "TYPE_DIRECTIVE")
        {
            if (unique_directives.find(val) != unique_directives.end())
            {
                if (seen_directives.find(val) != seen_directives.end())
                    report_parse_error("Duplicate directive in location", stream_lexems, i, val.c_str());
                seen_directives.insert(val);
            }
            parseDirective(obj_Location, val, stream_lexems, i);
        }
        else
            report_parse_error("Syntax Error: ", stream_lexems, i, "'Expected a directive' in parsLocation");
    }

    if (i >= stream_lexems.size() || stream_lexems[i].get_token_type() != "TYPE_RBRACE")
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Expected '}' at end of location block' in parsLocation");
    i++;
}

void httpConfig::parseDirective(Location &location, const std::string &directive, std::vector<Lexer> &stream, size_t &i)
{
    std::vector<std::string> values = consumeValues(i, stream);

    if (values.empty())
        report_parse_error("Directive '" + directive + "' must have at least one value.", stream, i, "parseDirective");

    if (directive == "root")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive", stream, i,
                "'root expects a single value' in parseDirective of Location");
        location.setRoot(values[0]);
    }
    else if (directive == "index")
    {
        if (values.empty())
            report_parse_error("Syntax Error: directive", stream, i,
                "'index expects at least one value' in parseDirective of Location");
        location.clear_index();
        for (size_t j = 0; j < values.size(); j++)
            location.setIndex_s(values[j]);
    }
    else if (directive == "allowed_methods")
    {
        if (values.empty())
            report_parse_error("Syntax Error: directive", stream, i,
                "'allowed_methods expects at least one method' in parseDirective of Location");
        std::set<std::string> seen_methods;
        for (size_t j = 0; j < values.size(); j++)
        {
            if (values[j] == "GET" || values[j] == "POST" || values[j] == "DELETE")
            {
                if (seen_methods.find(values[j]) != seen_methods.end())
                    report_parse_error("Syntax Error: directive", stream, i,
                        "'duplicate method' in parseDirective of Location");
                seen_methods.insert(values[j]);
                location.setMethods(values[j]);
            }
            else
                report_parse_error("Syntax Error: directive", stream, i,
                    "'not a valid method' in parseDirective of Location");
        }
    }
    else if (directive == "autoindex")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive", stream, i,
                "'autoindex expects a single value' in parseDirective of Location");
        if (values[0] == "on" || values[0] == "off")
            location.setAutoindex(values[0]);
        else
            report_parse_error("Syntax Error: directive", stream, i,
                "'NOT a valid option for autoindex' in parseDirective of Location");
    }
    else if (directive == "cgi_path")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive", stream, i,
                "'cgi_path expects a single value' in parseDirective of Location");
        location.setCGI_path(values[0]);
    }
    else if (directive == "cgi_ext")
    {
        if (values.empty())
            report_parse_error("Syntax Error: directive", stream, i,
                "'cgi_ext expects at least one literal value' in parseDirective of Location");
        location.setCGI_extensions(values);
    }
    else if (directive == "upload_store" || directive == "upload_path")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive", stream, i,
                "'upload_store/upload_path expects a single value' in parseDirective of Location");
        location.setUploadStore(values[0]);
    }
    else if (directive == "client_max_body_size")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive", stream, i,
                "'client_max_body_size expects a single value' in parseDirective of Location");
        long long tmp = parse_cl_mx_bd_sz(values[0]);
        if (tmp == -1 || tmp > MAX_CLIENT_BODY_SIZE_LIMIT)
            report_parse_error("Syntax Error: directive", stream, i,
                "'NOT a valid client max body size' in parseDirective of Location");
        location.setClientMaxBodySize(tmp);
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
            report_parse_error("Syntax Error: directive", stream, i,
                "'error_page needs at least a code and a path' in parseDirective of Location");
        std::string error_path = values.back();
        for (size_t j = 0; j < values.size() - 1; j++)
        {
            long code_long;
            if (!safe_strtol(values[j], code_long))
                report_parse_error("Syntax Error: directive", stream, i,
                    "'Invalid error code' in parseDirective of Location");
            
            int code = static_cast<int>(code_long);
            if (code < 300 || code > 599)
                report_parse_error("Syntax Error: directive", stream, i,
                    "'Invalid error code' in parseDirective of Location");
            
            location.set_error_page_loc(code, error_path);
        }
    }
    else if (directive == "return")
    {
        if (values.size() != 2)
            report_parse_error("Syntax Error: directive", stream, i,
                "'return expects exactly a code and a URL' in parseDirective of Location");
        
        long code_long;
        if (!safe_strtol(values[0], code_long))
            report_parse_error("Syntax Error: directive", stream, i,
                "'Invalid return code' in parseDirective of Location");
        
        int code = static_cast<int>(code_long);
        if (code < 300 || code >= 400)
            report_parse_error("Syntax Error: directive", stream, i,
                "'Invalid return code: must be 3xx' in parseDirective of Location");
        location.setReturnRedirection(code, values[1]);
    }
    else
    {
        report_parse_error("Syntax Error: directive", stream, i,
            "'UNKNOWN location directive' in parseDirective of Location");
    }
}
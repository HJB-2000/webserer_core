#include "httpConfig.hpp"
#include <iostream>
#include <cstdlib>
#include <set>

void httpConfig::parsLocation(Location &obj_Location, std::vector<Lexer> &stream_lexems, size_t &i)
{
    if(i < stream_lexems.size() && stream_lexems[i].get_value() == "location")
        i++;
    if (i < stream_lexems.size() && stream_lexems[i].get_token_type() == "TYPE_VALUE")
    {
        obj_Location.setPath(stream_lexems[i].get_value());
        i++;
    }
    else
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Expected path after location' in parsLocation");

    if (i >= stream_lexems.size() || stream_lexems[i].get_token_type() != "TYPE_LBRACE")
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Expected '{' after location path' in parsLocation");
    i++;

    std::set<std::string> seen_directives;
    std::set<std::string> unique_directives;
    unique_directives.insert("allowed_methods");
    unique_directives.insert("root");
    unique_directives.insert("index");
    unique_directives.insert("autoindex");
    unique_directives.insert("cgi_path");
    unique_directives.insert("cgi_pass");
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
        report_parse_error("Syntax Error: directive", stream, i, "'has no value' in parseDirective of Location");

    if (directive == "root")
    {
        location.setRoot(values[0]);
    }
    else if (directive == "index")
    {
        location.clear_index();
        for(size_t j = 0; j < values.size(); j++)
            location.setIndex_s(values[j]);
    }
    else if (directive == "allowed_methods")
    {
        for (size_t j = 0; j < values.size(); j++)
        {
            if(values[j] == "GET" || values[j] == "POST" || values[j] == "DELETE")
                location.setMethods(values[j]);
            else
                report_parse_error("Syntax Error: directive", stream, i, "'not a valid method' in parseDirective of Location");
        }
    }
    else if (directive == "autoindex")
    {
        if(values[0] == "on" || values[0] == "off")
            location.setAutoindex(values[0]);
        else
            report_parse_error("Syntax Error: directive", stream, i, "'NOT a valid option for autoindex' in parseDirective of Location");
    }
    else if (directive == "cgi_path" || directive == "cgi_pass")
    {
        location.setCGI_path(values[0]);
    }
    else if (directive == "cgi_ext")
    {
        location.setCGI_extension(values[0]);
    }
    else if (directive == "upload_store" || directive == "upload_path")
    {
        location.setUploadStore(values[0]);
    }
    else if (directive == "client_max_body_size")
    {
        long long tmp = parse_cl_mx_bd_sz(values[0]);
        if(tmp == -1)
            report_parse_error("Syntax Error: directive", stream, i, "'NOT a valid client max body size' in parseDirective of Location");
        location.setClientMaxBodySize(tmp);
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
            report_parse_error("Syntax Error: directive", stream, i, "'error_page needs at least a code and a path' in parseDirective of Location");
        std::string error_path = values.back();
        for (size_t j = 0; j < values.size() - 1; j++)
        {
            if(is_valid_number(values[j]))
            {
                int code = atoi(values[j].c_str());
                if (code < 300 || code > 599)
                    report_parse_error("Syntax Error: directive", stream, i, "'Invalid error code' in parseDirective of Location");
                location.set_error_page_loc(code, error_path);
            }
            else
                report_parse_error("Syntax Error: directive", stream, i, "'Invalid error code' in parseDirective of Location");
        }
    }
    else if (directive == "return")
    {
        location.setReturnRedirection(atoi(values[0].c_str()), values[1]);
    }
    else
    {
        report_parse_error("Syntax Error: directive", stream, i, "'UNKNOWN location directive' in parseDirective of Location");
    }
}

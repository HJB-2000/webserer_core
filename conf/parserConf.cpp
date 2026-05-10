#include "parserConf.hpp"
#include <iostream>
#include <cstdlib>
#include <set>
#include <stdexcept>

ParserConf::ParserConf() : _exist_http(false), _exist_server(false)
{
}

ParserConf::ParserConf(bool default_conf)
{
    (void) default_conf;
    httpConfig http_block;
    http_block.set_default_conf();
    this->_http = http_block;
    _exist_http = false;
    _exist_server = false;
}

ParserConf::~ParserConf()
{
}

httpConfig& ParserConf::get_http()
{
    return _http;
}

void ParserConf::parsHTTP(std::vector<Lexer> &stream_lexems, size_t &i)
{
    i++;
    size_t len = stream_lexems.size();
    if(i >= len || stream_lexems[i].get_token_type() != "TYPE_LBRACE")
    {
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Expected '{' after http' in parsHTTP");
    }
    i++;
    while(i < len && stream_lexems[i].get_token_type() != "TYPE_RBRACE")
    {
        std::string type = stream_lexems[i].get_token_type();
        std::string val = stream_lexems[i].get_value();
        if (type == "TYPE_CONTEXT" && val == "server")
        {
            Server new_server(_http);
            set_exist_server();
            _http.parsServer(new_server, stream_lexems, i);
            _http.set_servers(new_server);
        }
        else if(type == "TYPE_DIRECTIVE")
        {
            parseDirective(_http, val, stream_lexems, i);
        }
        else
        {
            report_parse_error("Syntax Error: ", stream_lexems, i, "'unexpected token during parsing context of http block' in parsHTTP");
        }
    }
    if(i >= len || stream_lexems[i].get_token_type() != "TYPE_RBRACE")
    {
        report_parse_error("Syntax Error: ", stream_lexems, i, "'Unclosed http block' in parsHTTP");
    }
    i++;
}

void ParserConf::parseDirective(httpConfig &http, const std::string &directive, std::vector<Lexer> &stream, size_t &i)
{
    std::vector<std::string> values = http.consumeValues(i, stream);
    if(values.empty())
    {
        report_parse_error("Syntax Error: directive ", stream, i, " 'has no value' parseDirective for http");
    }
    else if(directive == "client_max_body_size")
    {
        if (values.size() != 1)
            report_parse_error("Syntax Error: directive ", stream, i,
                "'client_max_body_size expects a single value' in parseDirective of http");
        long long tmp = parse_cl_mx_bd_sz(values[0]);
        if(tmp == -1 || tmp > MAX_CLIENT_BODY_SIZE_LIMIT)
        {
            report_parse_error("Syntax Error", stream, i, "Invalid client_max_body_size");
        }
        http.set_cl_mx_bd_sz(tmp);
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
            http.set_error_page(code, error_path);
        }
    }
    else
    {
        report_parse_error("Syntax Error: directive ", stream, i, " 'NOT SUPPORTED in http' parseDirective for http");
    }
}

bool is_valid_size(const std::string str)
{
    int dot = 0;
    size_t len = str.length();
    if (len == 0)
        return false;

    size_t num_end = len;
    if (isalpha(static_cast<unsigned char>(str[len - 1])))
    {
        if (len < 2 || !isdigit(static_cast<unsigned char>(str[len - 2])))
        {
            return false;
        }
        num_end = len - 1;
    }

    for (size_t i = 0; i < num_end; i++)
    {
        if (str[i] == '.')
        {
            dot++;
            if (dot > 1)                return false;
            if (i == 0)                 return false; // leading dot: ".5M"
            if (i + 1 >= num_end || !isdigit(static_cast<unsigned char>(str[i + 1])))
                return false;
            continue;
        }
        if (!isdigit(static_cast<unsigned char>(str[i])))
            return false;
    }
    return true;
}
 

#include <climits>
#include <cerrno>

long long parse_cl_mx_bd_sz(std::string val)
{
    char* end;
    if(!is_valid_size(val))
        return -1;

    errno = 0;
    double value = strtod(val.c_str(), &end);

    if (errno == ERANGE || value < 0)
        return -1;

    long long multiplier = 1;
    switch (*end) {
        case 'k': case 'K': multiplier = 1024LL;         break;
        case 'M': case 'm': multiplier = 1024LL * 1024;  break;
        case 'G': case 'g': multiplier = 1024LL * 1024 * 1024; break;
        case 0:             multiplier = 1;               break;
        default:            return -1;
    }
    if (multiplier == 1 && value != (long long)value)
        return -1;
    if (value > (double)LLONG_MAX / multiplier)
        return -1;

    return (long long)(value * multiplier);
}

void report_parse_error(const std::string& msg, std::vector<Lexer>& stream, size_t i, const char* where)
{
    const char* RED   = "\033[31m";
    const char* YEL   = "\033[33m";
    const char* RESET = "\033[0m";

    std::cerr << RED << "[Parse error]" << RESET;
    if (where && *where)
        std::cerr << " " << YEL << "(" << where << ")" << RESET;
    std::cerr << ": " << msg << std::endl;

    std::cerr << "  at token #" << i;
    if (i < stream.size())
        std::cerr << " [" << stream[i].get_token_type() << "=" << stream[i].get_value() << "]";
    std::cerr << std::endl;

    if (i > 0)
        std::cerr << "  prev: #" << (i - 1) << " [" << stream[i - 1].get_token_type() << "=" << stream[i - 1].get_value() << "]" << std::endl;
    if (i + 1 < stream.size())
        std::cerr << "  next: #" << (i + 1) << " [" << stream[i + 1].get_token_type() << "=" << stream[i + 1].get_value() << "]" << std::endl;

    throw std::runtime_error(msg);
}

void ParserConf::check_for_blocks()
{
    if(!get_exist_http() || !get_exist_server())
    {
        std::cerr << "Missing required http or server block" << std::endl;
        throw std::runtime_error("Missing required top-level blocks");
    }
}

void ParserConf::set_exist_http()   { this->_exist_http = true; }
bool ParserConf::get_exist_http()   { return _exist_http; }
void ParserConf::set_exist_server() { this->_exist_server = true; }
bool ParserConf::get_exist_server() { return _exist_server; }

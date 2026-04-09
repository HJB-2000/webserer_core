#ifndef HTTP_CONFIG_HPP
#define HTTP_CONFIG_HPP

#include <string>
#include <map>
#include <vector>
#include "serverConfig.hpp"
#include "LexerConfig.hpp"

class httpConfig
{
    public:
        httpConfig();
        httpConfig(const httpConfig& obj);
        httpConfig& operator=(const httpConfig& obj);
        ~httpConfig();

        void set_cl_mx_bd_sz(long long cl_mx_bd_sz);
        long long get_cl_mx_bd_sz() const;
        void set_error_page(int err_code, std::string err_path);
        std::map<int, std::string> get_error_page() const;
        // parsing
        // for server
        void parsServer(Server &obj_Server, std::vector<Lexer> &stream_lexems, size_t &i);
        void parseDirective(Server &server, const std::string &directive, std::vector<Lexer> &stream, size_t &i);

        // for location
        void parsLocation(Location &obj_Location, std::vector<Lexer> &stream_lexems, size_t &i);
        void parseDirective(Location &location, const std::string &directive, std::vector<Lexer> &stream, size_t &i);

        std::vector<std::string> consumeValues(size_t &i, std::vector<Lexer> &stream);
        std::vector<std::string> consumeDirective(size_t &i, std::vector<Lexer> &stream);

        // servers vector
        void set_servers(Server& server);
        const std::vector<Server>& get_all_servers() const;

        void set_default_conf();
        void set_exist_location();
        bool get_exist_location();

    private:
        long long _client_max_body_size;
        std::map<int, std::string> _error_page;
        std::vector<Server> _all_servers;
        bool _exist_location;
};
long long parse_cl_mx_bd_sz(std::string val);
bool is_valid_number(const std::string str);
void report_parse_error(const std::string& msg, std::vector<Lexer>& stream, size_t i, const char* where);
#endif

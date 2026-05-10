#ifndef PARSER_CONF_HPP
#define PARSER_CONF_HPP
/*
    ParserConf — top-level config parser.

    Handles the http { } block and server/location sub-blocks.
    The events { } block (event model, worker_connections) is
    intentionally skipped: the core EventLoop owns those concerns.
*/
#include "locationConfig.hpp"
#include "serverConfig.hpp"
#include "LexerConfig.hpp"
#include "httpConfig.hpp"
#include "eventsConfig.hpp"

class ParserConf
{
    public:
        ParserConf();
        ParserConf(bool default_conf);
        ~ParserConf();

    void parsHTTP(std::vector<Lexer> &stream_lexems, size_t &i);
    void parseDirective(httpConfig &http, const std::string &directive, std::vector<Lexer> &stream, size_t &i);
    void parseEvents(eventsConfig &obj_events, std::vector<Lexer> &stream_lexems, size_t &i);
    void parseDirective(eventsConfig &events, const std::string &directive, std::vector<Lexer> &stream, size_t &i);

        httpConfig& get_http();

        void check_for_blocks();

        void set_exist_http();
        bool get_exist_http();

        void set_exist_server();
        bool get_exist_server();
        void printer_of_conf_parser();
    private:
    httpConfig _http;
    eventsConfig _events;
        bool _exist_http;
        bool _exist_server;
};

void report_parse_error(const std::string& msg, std::vector<Lexer>& stream, size_t i, const char* where);
#endif

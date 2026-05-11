#include "eventsConfig.hpp"
#include "parserConf.hpp"
#include <iostream>
#include <set>

static std::vector<std::string> consumeValuesLenient(size_t &i, std::vector<Lexer> &stream)
{
    std::vector<std::string> values;
    i++;

    while (i < stream.size() && stream[i].get_token_type() == "TYPE_VALUE")
    {
        values.push_back(stream[i].get_value());
        i++;
    }

    if (i < stream.size() && stream[i].get_token_type() == "TYPE_SEMICOLON")
    {
        i++;
    }
    else if (i < stream.size())
    {
        report_parse_error("Missing ';' after events directive", stream, i, "consumeValuesLenient");
    }
    else
    {
        report_parse_error("Unexpected end of file: expected ';' after events directive", stream, i - 1,
                           "consumeValuesLenient");
    }

    return values;
}

eventsConfig::eventsConfig() : _worker_connections(0), _event_model("")
{
}

eventsConfig::eventsConfig(const eventsConfig& obj)
{
    this->_worker_connections = obj._worker_connections;
    this->_event_model = obj._event_model;
}
eventsConfig& eventsConfig::operator=(const eventsConfig& obj)
{
    if(this != &obj)
    {
        this->_worker_connections = obj._worker_connections;
        this->_event_model = obj._event_model;
    }
    return (*this);
}

eventsConfig::~eventsConfig()
{
}
void   eventsConfig::set_worker_connections(int work_connets)
{
    this->_worker_connections = work_connets;
}
void   eventsConfig::set_event_model(std::string event_model)
{
    this->_event_model = event_model;

}
int eventsConfig::get_worker_connections()
{
    return _worker_connections;
}
std::string eventsConfig::get_event_model()
{
    return _event_model;
}

void eventsConfig::set_default_conf()
{
    _worker_connections = 1;
    _event_model = "epoll";
}

void ParserConf::parseDirective(eventsConfig &events, const std::string &directive, std::vector<Lexer> &stream, size_t &i)
{
    std::vector<std::string> values = consumeValuesLenient(i, stream);

    if(values.empty())
    {
        std::cerr << "[WARN] events: directive '" << directive
                  << "' has no values" << std::endl;
        return;
    }
    if(directive == "worker_connections")
    {
        if (values.size() != 1)
        {
            std::cerr << "[WARN] events: worker_connections expects a single value" << std::endl;
            return;
        }
        long tmp = 0;
        if (!safe_strtol(values[0], tmp) || tmp <= 0)
        {
            std::cerr << "[WARN] events: invalid worker_connections value '"
                      << values[0] << "'" << std::endl;
            return;
        }
        events.set_worker_connections(static_cast<int>(tmp));
    }
    else if(directive == "event_model")
    {
        if (values.size() != 1)
        {
            std::cerr << "[WARN] events: event_model expects a single value" << std::endl;
            return;
        }
        if(values[0] == "select" || values[0] == "poll" || values[0] == "epoll" || values[0] == "kqueue")
            events.set_event_model(values[0]);
        else
            std::cerr << "[WARN] events: unsupported event_model '" << values[0] << "'" << std::endl;
    }
    else
    {
        std::cerr << "[WARN] events: unknown directive '" << directive << "'" << std::endl;
    }
}

void ParserConf::parseEvents(eventsConfig &obj_events, std::vector<Lexer> &stream_lexems, size_t &i)
{
    i++;
    size_t len = stream_lexems.size();
    if(i >= len || stream_lexems[i].get_token_type() != "TYPE_LBRACE")
    {
        report_parse_error("Expected '{' after events", stream_lexems, i, "parseEvents");
    }
    i++;
    std::set<std::string> seen_directives;
    std::set<std::string> unique_directives;
    unique_directives.insert("worker_connections");
    unique_directives.insert("event_model");

    bool saw_worker = false;
    bool saw_model = false;

    while(i < len && stream_lexems[i].get_token_type() != "TYPE_RBRACE")
    {
        std::string type = stream_lexems[i].get_token_type();
        std::string val = stream_lexems[i].get_value();
        if(type == "TYPE_LBRACE")
            report_parse_error("Syntax Error: cant have nested context", stream_lexems, i, "parseEvents");

        if(type == "TYPE_DIRECTIVE")
        {
            if (unique_directives.find(val) != unique_directives.end())
            {
                if (seen_directives.find(val) != seen_directives.end())
                {
                    std::cerr << "[WARN] events: duplicate directive '" << val << "'" << std::endl;
                }
                seen_directives.insert(val);
            }
            if (val == "worker_connections")
                saw_worker = true;
            else if (val == "event_model")
                saw_model = true;
            parseDirective(obj_events, val, stream_lexems, i);
        }
        else
        {
            std::cerr << "[WARN] events: unexpected token '" << stream_lexems[i].get_value()
                      << "'" << std::endl;
            i++;
        }
    }
    if(i >= len || stream_lexems[i].get_token_type() != "TYPE_RBRACE")
        report_parse_error("Syntax Error: Unclosed events block", stream_lexems, i, "parseEvents");
    if (!saw_worker || !saw_model)
    {
        std::cerr << "[WARN] events: missing directives:";
        if (!saw_worker)
            std::cerr << " worker_connections";
        if (!saw_model)
            std::cerr << " event_model";
        std::cerr << std::endl;
    }
    i++;
}

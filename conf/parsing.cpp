#include "parsing.hpp"
#include <iostream>
#include <cctype>

void remove_comments(std::stringstream &buff)
{
    std::string str = buff.str();
    char hashtag = '#';
    std::string out;
    for(size_t i = 0; i < str.length(); i++)
    {
        if(str[i] == hashtag)
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
    buff.clear();
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
    buff.clear();
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
    buff.clear();
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

// Skip past a balanced { } block starting at stream_lexems[i]
// (i should be pointing at TYPE_LBRACE on entry; exits after TYPE_RBRACE).
static void skipBlock(std::vector<Lexer>& stream, size_t& i)
{
    size_t len = stream.size();
    // find opening brace
    while (i < len && stream[i].get_token_type() != "TYPE_LBRACE")
        i++;
    if (i >= len) return;
    i++; // consume '{'
    int depth = 1;
    while (i < len && depth > 0)
    {
        std::string t = stream[i].get_token_type();
        if (t == "TYPE_LBRACE")  depth++;
        else if (t == "TYPE_RBRACE") depth--;
        i++;
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
            // events { } is the core's territory — skip entirely.
            i++;
            skipBlock(stream_lexems, i);
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
}

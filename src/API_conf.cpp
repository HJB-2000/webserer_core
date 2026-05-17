// ============================================================
//  src/API_conf.cpp
//
//  Config parser API — called once from main().
//  Returns the full list of parsed ServerConfig objects.
// ============================================================

#include "Headers/API_conf.hpp"

#include "parserConf.hpp"
#include "parsing.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>

std::vector<ServerConfig> API_conf(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "[API_conf] no config file given — using default config\n";
        ServerConfig s;
        s.set_default_conf();
        std::vector<ServerConfig> servers;
        servers.push_back(s);
        return servers;
    }
    
    const std::string config_path(argv[1]);
    if (config_path.size() < 5 || config_path.substr(config_path.size() - 5) != ".conf")
    {
        throw std::runtime_error(std::string("[API_conf] invalid config extension (expected .conf)"));
    }
    std::ifstream config_file(config_path.c_str());
    try
    {
        if (!config_file.is_open())
        {
            throw std::runtime_error(std::string("[API_conf] cannot open the config file"));
        }

        std::stringstream buff;
        buff << config_file.rdbuf();
        if (buff.str().empty())
        {
            // std::cerr << "[API_conf] config file is empty: " << config_path << "\n";
            config_file.close();
            throw std::runtime_error(std::string("[API_conf] config file is empty"));
        }
        check_valid_content(buff);
        insert_space(buff);
        remove_comments(buff);
        refactoring_buffer(buff);

        if (buff.str().find_first_not_of(" \t\r\n") == std::string::npos)
        {
            buff.str("");
            buff.clear();
            config_file.close();
            throw std::runtime_error(std::string("[API_conf] config has no directives after preprocessing"));
        }

        std::vector<std::string> tokens = storing_in_vec(buff);

        Lexer::init_grammar();
        std::vector<Lexer> stream;
        for (size_t i = 0; i < tokens.size(); ++i)
            if (!tokens[i].empty())
                stream.push_back(Lexer(Lexer::identify(tokens[i]), tokens[i]));
        stream.push_back(Lexer(TYPE_END, ""));

        
            ParserConf parser;
            parsing_lexems(parser, stream);
            return parser.get_http().get_all_servers();
    }
    catch (const std::runtime_error& e)
    {
        config_file.close();
        // std::cerr << "[API_conf] parse error:" << e.what() << "\n";
        throw;
    }
}

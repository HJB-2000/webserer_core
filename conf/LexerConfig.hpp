#ifndef LEXER_CONFIG_HPP
#define LEXER_CONFIG_HPP

#include <string>
#include <map>
typedef enum token_type
{
    TYPE_CONTEXT,
    TYPE_DIRECTIVE,
    TYPE_VALUE,    
    TYPE_LBRACE,   
    TYPE_RBRACE,   
    TYPE_SEMICOLON,
    TYPE_ERROR,    
    TYPE_END       
} t_token_type;

class Lexer
{
    public :
        Lexer(t_token_type type, std::string value);
        Lexer(const Lexer& obj);
        Lexer& operator=(const Lexer& obj);
        std::string get_value();
        std::string get_token_type();
        void set_grammar();
        ~Lexer();

        static t_token_type identify(const std::string& s);
        static void init_grammar();

    private:
        t_token_type _type;
        std::string _value;
        static std::map<std::string, t_token_type> _grammar;

};
#endif
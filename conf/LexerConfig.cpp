#include "LexerConfig.hpp"

std::map<std::string, t_token_type> Lexer::_grammar;

Lexer::Lexer(t_token_type type, std::string value) : _type(type), _value(value) 
{

}

Lexer::~Lexer() 
{

}

Lexer::Lexer(const Lexer& obj)
{
    this->_type = obj._type;
    this->_value = obj._value;
}

Lexer& Lexer::operator=(const Lexer& obj)
{
    if(this != &obj)
    {
        this->_type = obj._type;
        this->_value = obj._value;
    }
    return (*this);
}

std::string Lexer::get_token_type()
{
    if(_type == TYPE_CONTEXT)
        return "TYPE_CONTEXT";    
    else if(_type == TYPE_DIRECTIVE)
        return "TYPE_DIRECTIVE";    
    else if(_type == TYPE_VALUE)
        return "TYPE_VALUE";    
    else if(_type == TYPE_LBRACE)
        return "TYPE_LBRACE";    
    else if(_type == TYPE_RBRACE)
        return "TYPE_RBRACE";    
    else if(_type == TYPE_SEMICOLON)
        return "TYPE_SEMICOLON";    
    else if(_type == TYPE_ERROR)
        return "TYPE_ERROR";    
    else if(_type == TYPE_END)
        return "TYPE_END";
    else 
        return "UKNOWN_TYPE";    
}

std::string Lexer::get_value()
{
    return _value;
}

void Lexer::init_grammar()
{
    _grammar["events"] = TYPE_CONTEXT;
    _grammar["http"] = TYPE_CONTEXT;
    _grammar["server"] = TYPE_CONTEXT;
    _grammar["location"] = TYPE_CONTEXT;
    
    _grammar["worker_connections"] = TYPE_DIRECTIVE;           
    _grammar["event_model"] = TYPE_DIRECTIVE;           
    _grammar["listen"] = TYPE_DIRECTIVE;           
    _grammar["host"] = TYPE_DIRECTIVE;           
    _grammar["server_name"] = TYPE_DIRECTIVE;      
    _grammar["root"] = TYPE_DIRECTIVE;             
    _grammar["index"] = TYPE_DIRECTIVE;            
    _grammar["client_max_body_size"] = TYPE_DIRECTIVE;  
    _grammar["error_page"] = TYPE_DIRECTIVE;       
    
    _grammar["allowed_methods"] = TYPE_DIRECTIVE;    
    _grammar["autoindex"] = TYPE_DIRECTIVE;        
    _grammar["return"] = TYPE_DIRECTIVE;           
    _grammar["upload_path"] = TYPE_DIRECTIVE;      
    _grammar["timeout"] = TYPE_DIRECTIVE;      
    
    _grammar["cgi_path"] = TYPE_DIRECTIVE;         
    _grammar["cgi_ext"] = TYPE_DIRECTIVE;          
    
    _grammar["{"] = TYPE_LBRACE;
    _grammar["}"] = TYPE_RBRACE;
    _grammar[";"] = TYPE_SEMICOLON;
}

t_token_type Lexer::identify(const std::string& s)
{

    std::map<std::string, t_token_type>::iterator it = _grammar.find(s);

    if (it != _grammar.end())
    {
        return it->second;
    }

    return TYPE_VALUE;
}


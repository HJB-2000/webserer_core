#ifndef PARSING_HPP
#define PARSING_HPP

#include <string>
#include <iosfwd>
#include <sstream>
#include <vector>

#include "parserConf.hpp"

void remove_comments(std::stringstream &buff);
void refactoring_buffer(std::stringstream &buff);
void insert_space(std::stringstream &buff);

std::vector<std::string> storing_in_vec(std::stringstream &buff);

// Top-level token dispatcher.
// events { } blocks are silently skipped (core owns that logic).
// http { } block is parsed into parser.get_http().
void parsing_lexems(ParserConf& parser, std::vector<Lexer>& stream_lexems);

void check_valid_content(std::stringstream &buff);
#endif

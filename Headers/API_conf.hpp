// ============================================================
//  Headers/API_conf.hpp
//
//  Single entry point for config parsing.
//  Takes argc/argv, returns the parsed server list.
//
//  Implementation: src/API_conf.cpp
// ============================================================
#ifndef API_CONF_HPP
#define API_CONF_HPP

#include <vector>
#include "serverConfig.hpp"

// Parses the config file given in argv[1] and returns one
// ServerConfig object per server block.
//
// Stub mode (BUG-T1 + BUG-T2 not yet fixed):
//   Returns a single default server on 127.0.0.1:8080.
//
// Real mode (uncomment the parser block in API_conf.cpp):
//   Reads argv[1], tokenizes, lexes, and parses the full
//   config pipeline → returns all server blocks.
std::vector<ServerConfig> API_conf(int argc, char** argv);

#endif // API_CONF_HPP

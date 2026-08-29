#pragma once 
#include "ServerConfig.hpp"
#include "ConfigFile.hpp"
#include "ConfigParser.hpp"

class Config
{
private:
    std::vector<ServerConfig> _servers;
public:
    Config();
    ~Config();
    bool parse(const std::string &filename);
};



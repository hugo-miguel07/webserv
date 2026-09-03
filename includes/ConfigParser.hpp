#pragma once
#include "test.hpp"
#include "ConfigFile.hpp"
#include "ServerConfig.hpp"

class ConfigParser  
{
private:
    
public:
    ConfigParser();
    ~ConfigParser();
    std::vector<std::string> tokenize(const std::string& input) const;
    std::vector<ServerConfig> parseServers(const std::vector<std::string>& tokens);
    ServerConfig              parseServerInfo(const std::vector<std::string>& tokens, size_t &i);

    
    size_t parseBodySizeWeight(const std::string& str);
    void   handle_location(const std::vector<std::string> &tokens, size_t &i);



};



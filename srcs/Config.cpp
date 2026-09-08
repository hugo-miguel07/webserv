#include "Config.hpp"

Config::Config() {}

Config::~Config() {}

bool Config::parse(const std::string& filename)
{
    ConfigFile confFile;
    ConfigParser configParser;
    std::vector<std::string> tokens;

    confFile.read_and_append(filename);
    tokens = configParser.tokenize(confFile.get_str());
    this->_servers = configParser.parseServers(tokens);

    return (true);
}

std::vector<ServerConfig> getServers(){
    return _servers;
}

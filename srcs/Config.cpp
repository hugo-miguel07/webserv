#include "Config.hpp"
#include "Tokenize.hpp"

Config::Config() {}

Config::~Config() {}

void Config::parse(const std::string& filename)
{
    ConfigFile confFile;
    ConfigParser configParser;
    Tokenize tokanizer;

    confFile.read_and_append(filename);
    this->_servers = configParser.parseServers(tokanizer.tokenize(confFile.get_str()));
    // std::vector<ServerConfig>::iterator it = _servers.begin();
    // int i = 1;
    // while (it != _servers.end())
    // {   
    //     std::cout << "Server ["<< i<< "] name: " << it->getServerName().at(0) << std::endl;
    //     std::cout << "Port: ["<< i<< "]"  << it->getPort() << std::endl;
    //     ++it;
    //     i++;
    // }
}

std::vector<ServerConfig> Config::getServers(){
    return (_servers);
}

#include "ServerConfig.hpp"

ServerConfig::ServerConfig() : _port(-1), _maxBodySize(0), _root(""), _host(""), _indexes(), _serverNames(), _locations()  {}

ServerConfig::~ServerConfig() {}

void ServerConfig::setPort(const int &port)
{
    _port = port;
}

void ServerConfig::setBodySize(const int &bodySize)
{
    _maxBodySize = bodySize;
}

void ServerConfig::setHost(const std::string &host)
{
    _host = host;
}


void ServerConfig::setIndex(const std::string &index)
{
    _indexes.push_back(index);
}

void ServerConfig::setRoot(const std::string &root)
{
    _root = root;
}
void ServerConfig::setServerName(const std::string &name)
{
    _serverNames.push_back(name);
}

int ServerConfig::getPort() const
{
    return (_port);
}

int ServerConfig::getBodySize() const
{
    return (_maxBodySize);
}
std::string ServerConfig::getRoot() const
{
    return (_root);
}
std::vector<std::string> ServerConfig::getIndex() const
{
    return (_indexes);
}
std::string ServerConfig::getHost() const
{
    return (_host);
}

std::vector<std::string> ServerConfig::getServerName() const
{
    return (_serverNames);
}


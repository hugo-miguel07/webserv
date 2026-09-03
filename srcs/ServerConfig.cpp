#include "ServerConfig.hpp"

ServerConfig::ServerConfig() : _port(-1), _maxBodySize(0), _root(""), _indexes(), _serverNames(), _locations(), _errorPages()  {}

ServerConfig::~ServerConfig() {}

void ServerConfig::setPort(const int &port)
{
    _port = port;
}

void ServerConfig::setBodySize(const size_t &bodySize)
{
    _maxBodySize = bodySize;
}

void ServerConfig::setIndex(const std::string &index)
{
    if (index.empty())
        return;
    _indexes.push_back(index);
}

void ServerConfig::setRoot(const std::string &root)
{
    if (root.empty())
        return;
    _root = root;
}
void ServerConfig::setServerName(const std::string &name)
{
    if (name.empty())
        return;
    _serverNames.push_back(name);
}
void ServerConfig::setErrorPages(const int& key, const std::string &value)
{
    _errorPages.insert(std::make_pair(key, value));
}

int ServerConfig::getPort() const
{
    return (_port);
}

size_t ServerConfig::getBodySize() const
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

std::vector<std::string> ServerConfig::getServerName() const
{
    return (_serverNames);
}

std::map<int, std::string> ServerConfig::getErrorPages() const
{
    return (_errorPages);
}


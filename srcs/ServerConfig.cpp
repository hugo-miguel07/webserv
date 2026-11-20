#include "ServerConfig.hpp"

ServerConfig::ServerConfig() : _port(8080), _maxBodySize(0), _root(""), _indexes(), _serverNames(), _locations(), _errorPages()  {}

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

void ServerConfig::addLocation(const Locations& location)
{
    _locations.push_back(location);
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

std::vector<Locations>  ServerConfig::getLocations() const
{
    return (_locations);
}

Locations ServerConfig::findLocation(const std::string& path, const std::string& method) const
{
    size_t prefix_match = _locations.size();
    size_t extension_match = _locations.size();
    for (size_t i = 0; i < _locations.size(); ++i)
    {
        const std::string pattern = _locations[i].getPath();
        if (pattern.empty())
            continue;
        if (pattern[0] == '.')
        {
            // Extension rules execute CGI for POST; GET keeps its prefix route.
            if (method == "POST" && path.size() >= pattern.size()
                && path.compare(path.size() - pattern.size(), pattern.size(), pattern) == 0
                && (extension_match == _locations.size()
                    || pattern.size() > _locations[extension_match].getPath().size()))
                extension_match = i;
            continue;
        }
        if (path.compare(0, pattern.size(), pattern) != 0)
            continue;
        if (path.size() != pattern.size() && pattern[pattern.size() - 1] != '/'
            && path[pattern.size()] != '/')
            continue;
        if (prefix_match == _locations.size()
            || pattern.size() > _locations[prefix_match].getPath().size())
            prefix_match = i;
    }
    if (extension_match != _locations.size())
    {
        Locations result = _locations[extension_match];
        if (prefix_match != _locations.size())
        {
            const Locations &parent = _locations[prefix_match];
            if (result.getRoot().empty())
                result.setRoot(parent.getRoot());
            if (result.getBodySize() == 0)
                result.setBodySize(parent.getBodySize());
        }
        return result;
    }
    if (prefix_match == _locations.size())
        throw 404;
    return _locations[prefix_match];
}


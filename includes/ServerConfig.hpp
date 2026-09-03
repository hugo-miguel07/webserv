#pragma once
#include "Locations.hpp"
class ServerConfig
{
private:
    int _port;
    size_t _maxBodySize;
    std::string _root;
    std::vector<std::string> _indexes;
    std::vector<std::string> _serverNames;
    std::vector<Locations> _locations;
    std::map<int, std::string> _errorPages;
    
public:
    ServerConfig();
    ~ServerConfig();

    size_t                      getBodySize() const;
    int                         getPort() const;
    std::string                 getRoot() const;
    std::vector<std::string>    getIndex() const;
    std::vector<std::string>    getServerName() const;
    std::map<int, std::string>  getErrorPages() const;

    void setPort(const int& port);
    void setBodySize(const size_t& bodySize);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setServerName(const std::string& name);
    void setLocations(); //??????
    void setErrorPages(const int& key, const std::string& value);

    //location
};



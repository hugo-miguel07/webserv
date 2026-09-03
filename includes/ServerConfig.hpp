#pragma once
#include "Locations.hpp"
class ServerConfig
{
private:
    int _port;
    int _maxBodySize;
    std::string _root;
    std::string _host;
    std::vector<std::string> _indexes;
    std::vector<std::string> _serverNames;
    std::vector<Locations> _locations;
public:
    ServerConfig();
    ~ServerConfig();

    int                         getBodySize() const;
    int                         getPort() const;
    std::string                 getRoot() const;
    std::string                 getHost() const;
    std::vector<std::string>    getIndex() const;
    std::vector<std::string>    getServerName() const;

    void setPort(const int& port);
    void setBodySize(const int& bodySize);
    void setRoot(const std::string& root);
    void setHost(const std::string& host);
    void setIndex(const std::string& index);
    void setServerName(const std::string& name);
    void setLocations(); //??????

    //location
    //errorPages
};



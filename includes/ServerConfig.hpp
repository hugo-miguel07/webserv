#pragma once
#include "Locations.hpp"
class ServerConfig
{
private:
    int _port;
    int _maxBodySize;
    std::string _root;
    std::string _host; //HOST?????????
    std::string _index;
    std::vector<std::string> _serverNames; //iterar server names
    std::vector<Locations> _locations;
public:
    ServerConfig();
    ~ServerConfig();

    int                         getPort() const;
    std::string                 getRoot() const;
    std::string                 getHost() const;
    std::string                 getIndex() const;
    int                         getBodySize() const;
    std::vector<std::string>    getServerName() const; //?????

    void setPort(const int& port);
    void setBodySize(const int& bodySize);
    void setHost(const std::string& host);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setServerName(const std::string& name); //?????
    void setLocations(); //??????

    //location
    //errorPages
};



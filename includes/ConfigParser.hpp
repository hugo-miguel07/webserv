#pragma once
#include "test.hpp"
#include "ConfigFile.hpp"
#include "ServerConfig.hpp"

class ConfigParser  
{
private:
    ServerConfig _serv;
    Locations    _loc;
public:
    ConfigParser();
    ~ConfigParser();
    std::vector<ServerConfig> parseServers(const std::vector<Token>& tokens);
    ServerConfig              parseServerInfo(const std::vector<Token>& tokens, size_t &i);

    bool isHttpStatusCodeSuported(const std::string& token);
    size_t parseBodySizeWeight(const std::string& str, const std::vector<Token>& token, size_t i);
    Locations   handleLocation(const std::vector<Token> &tokens, size_t &i);
    bool   isDirective(const std::string& token);
    bool   isLocationDirective(const std::string& token);
    DirectiveType getDirectiveType(const std::string& token);
    DirectiveType getLocationDirectiveType(const std::string& token);
    void debugLocations();
    void debugServers();




    //DIRECTIVE HANDLERS
    void handleListenDirective(const std::vector<Token>& tokens, size_t &i);
    void handleRootDirective(const std::vector<Token>& tokens, size_t &i, bool is_location);
    void handleIndexDirective(const std::vector<Token>& tokens, size_t &i, bool is_location);
    void handleServerNameDirective(const std::vector<Token>& tokens, size_t &i);
    void handleClientMaxBodySizeDirective(const std::vector<Token>& tokens, size_t &i);
    void handleErrorPageDirective(const std::vector<Token>& tokens, size_t &i);
    void handleLocationDirective(const std::vector<Token>& tokens, size_t &i);
    void handleAllowMethodsDirective(const std::vector<Token>& tokens, size_t &i);
    void handleReturnDirective(const std::vector<Token>& tokens, size_t &i);
    void handleAutoindexDirective(const std::vector<Token>& tokens, size_t &i);
    void handleUploadStoreDirective(const std::vector<Token>& tokens, size_t &i);
    void handleCgiExtensionDirective(const std::vector<Token>& tokens, size_t &i);
    void handleCgiPathDirective(const std::vector<Token>& tokens, size_t &i);



};



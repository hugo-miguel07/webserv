#pragma once
#include "libs.hpp"
#include "ServerConfig.hpp"
#include "Listener.hpp"

class Client
{
private:
    int         _fd;
    std::string _requestBuffer;
    std::string _responseBuffer;
    size_t      _responseOffset;
    bool        _responseReady;
    Listener*   _listener;
    const ServerConfig* _config;

public:
                        Client();
                        ~Client();
                        Client(int client_fd, Listener *listener);
    int                 getFd() const;
    void                setFd(int fd);
    const Listener&     getListener() const;
    void                appendRequest(const char *buffer, int total_bytes);
    void                appendResponse(const char *buffer, int total_bytes);
    const std::string&  getResponseBuffer() const;
    const std::string&  getRequestBuffer() const;
    void                setResponseOffset(size_t value);
    bool                getResponseReady() const ;
    void                setResponseReady(bool flag);
    size_t              getResponseOffset() const;
    void                setConfig(const ServerConfig& config);
    const ServerConfig& getConfig() const;


    bool                requestComplete();

};

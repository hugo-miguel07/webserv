#pragma once
#include "test.hpp"

class Client
{
private:
    int         _fd;
    std::string _requestBuffer;
    std::string _responseBuffer;
    size_t      _responseOffset;
    bool        _responseReady;

public:
                        Client();
                        ~Client();
                        Client(int client_fd);
    int                 get_fd() const;
    void                appendRequest(const char *buffer, int total_bytes);
    void                appendResponse(const char *buffer, int total_bytes);
    const std::string&  getResponseBuffer() const;
    const std::string&  getRequestBuffer() const;
    void                setResponseOffset(size_t value);
    bool                getResponseReady() const ;
    void                setResponseReady(bool flag);
    size_t              getResponseOffset() const;

    bool                requestComplete();


    void              mirror(); //apagar dps
};

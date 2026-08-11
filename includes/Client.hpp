#pragma once
#include "test.hpp"

class Client
{
private:
    int _client_fd;
    sockaddr_in _client_addr;
    std::string _requestBuffer;
public:
    Client();
    ~Client();
};

Client::Client(/* args */)
{
}

Client::~Client()
{
}

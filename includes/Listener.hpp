#pragma once

#include "test.hpp"
#include "ServerConfig.hpp"

class Listener
{
public:
    int fd;
    int port;
    std::vector<ServerConfig> configs;

    Listener();
    ~Listener();
};



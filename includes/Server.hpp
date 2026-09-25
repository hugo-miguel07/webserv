#pragma once

#include "test.hpp"
#include "Client.hpp"
#include "Config.hpp"

class Server
{
private:
    std::vector<int>            _server_fds;
    std::vector<Client>         _clients;
    std::vector<struct pollfd>  _pollfds;
    std::vector<Client>         _removeClients;
    Config                      _config;
public:
    Server(const Config& config);
    ~Server();

    bool        init();
    void        acceptClient(int fd);
    void        run();
    void        shut_down();
    Client*     getClientById(int fd);

private:
    bool isServerFd(int fd);
    void handleRead(Client &client);
    void handleWrite(Client &client);
    void removeClients();
    void enablePollOut();
    void disablePollOut(int fd);
};


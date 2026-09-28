#pragma once

#include "libs.hpp"
#include "Client.hpp"
#include "Config.hpp"
#include "Listener.hpp"

class Server
{
private:
    std::vector<int>            _server_fds;
    std::vector<Client>         _clients;
    std::vector<struct pollfd>  _pollfds;
    std::vector<int>            _removeClients;
    Config                      _config;
    std::vector<Listener>       _listeners;
public:
    Server(const Config& config);
    ~Server();

    bool        init();
    void        acceptClient(int fd);
    void        run();
    void        shut_down();
    Client*     getClientById(int fd);

private:
    Listener* FindListener(int port);
    bool isServerFd(int fd);
    void handleRead(Client &client);
    void handleWrite(Client &client);
    void removeClients();
    void enablePollOut();
    void disablePollOut(int fd);
};


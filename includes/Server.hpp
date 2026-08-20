#pragma once

#include "test.hpp"
#include "Client.hpp"

class Server
{
private:
    int                         _server_fd;
    int                         _port;
    std::vector<Client>         _clients;
    std::vector<struct pollfd>  _pollfds;
    std::vector<Client>         _removeClients;

public:
    Server(int port);
    ~Server();

    bool        init();
    int         getServerFd() const;
    void        acceptClient();
    void        run();
    void        shut_down();
    Client*     getClientById(int fd);

private:
    void handleRead(Client &client);
    void handleWrite(Client &client);
    void removeClients();
    void enablePollOut();
    void disablePollOut(int fd);
};


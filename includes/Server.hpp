#pragma once

#include "libs.hpp"
#include "Client.hpp"
#include "Config.hpp"
#include "Listener.hpp"
#include "Locations.hpp"
#include "Request.hpp"
#include <ctime>

class Server
{
private:
    std::vector<int>            _server_fds;
    std::vector<Client>         _clients;
    std::map<int, Request>       _requests;
    std::map<int, std::time_t>   _drainingClients;
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
    Locations findLocation(const ServerConfig &config, const std::string &path,
                           const std::string &method) const;
    void handleRead(Client &client);
    void handleWrite(Client &client);
    void removeClients();
    void enablePollOut();
    void disablePollOut(int fd);
};

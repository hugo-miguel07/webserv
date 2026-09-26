#include "Server.hpp"
#include "Client.hpp"
#include "Request.hpp"

Server::Server(const Config &config) : _config(config) {}

Server::~Server() { this->shut_down(); }

bool Server::init() 
{
    const std::vector<ServerConfig>& servers = _config.getServers();
    for (std::vector<ServerConfig>::const_iterator it = servers.begin(); it != servers.end(); ++it)
    {
        Listener * listener = FindListener(it->getPort());
        if (listener)
        {
            listener->configs.push_back(*it);
            continue;
        }
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
            throw(std::runtime_error("Error on socket()"));
        if (fcntl(server_fd, F_SETFL, O_NONBLOCK) == -1)
        {
            close(server_fd);
            throw(std::runtime_error("Error on fnctl()"));
        }
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(it->getPort());
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
        {
            close(server_fd);
            throw(std::runtime_error("Error on bind()"));
        }
        if (listen(server_fd, SOMAXCONN) == -1)
        {
            close(server_fd);
            throw(std::runtime_error("Error on listen()"));
        }
        Listener newListener;
        newListener.port = it->getPort();
        newListener.fd = server_fd;
        newListener.configs.push_back(*it);
        _listeners.push_back(newListener);
        _server_fds.push_back(server_fd);

        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _pollfds.push_back(pfd);
        std::cout << "Server initialized on port: " << it->getPort() << std::endl;
    }
    return (true);
}

bool Server::isServerFd(int fd)
{
    for (std::vector<int>::iterator it = _server_fds.begin(); it != _server_fds.end(); ++it)
        if (*it == fd)
            return true;
    return false;
}

void Server::enablePollOut()
{
    for (size_t i = 0; i < _pollfds.size(); i++)
    {
        if (isServerFd(_pollfds[i].fd))
            continue;
        Client *client = getClientById(_pollfds[i].fd);
        if (client && client->getResponseReady())
            _pollfds[i].events |= POLLOUT;
        else
            _pollfds[i].events &= ~POLLOUT;
    }
}

void Server::run()
{
    while (g_running)
    {
        Server::enablePollOut();
        int ret = poll(_pollfds.data(), _pollfds.size(), -1);
        if (ret == -1)
        {
            if (g_running)
                throw (std::runtime_error("Error on poll()"));
            else
                throw (std::runtime_error("Process finished by SIGINT"));
            break;
        }

        size_t size = _pollfds.size();
        for (size_t i = 0; i < size; i++)
        {
            if (_pollfds[i].revents & POLLIN)
            {  
                if (isServerFd(_pollfds[i].fd))
                    Server::acceptClient(_pollfds[i].fd);
                else
                {
                    Client *client = getClientById(_pollfds[i].fd);
                    if (client)
                        Server::handleRead(*client);
                }
            }
            if (_pollfds[i].revents & POLLOUT)
            {
                Client *client = getClientById(_pollfds[i].fd);
                if (client)
                    Server::handleWrite(*client);
            }
        }
        Server::removeClients();
    }
    Server::shut_down();
}

void Server::removeClients()
{
    if (_removeClients.empty())
        return ;
    for(std::vector<int>::iterator remove_it = _removeClients.begin(); remove_it != _removeClients.end(); ++remove_it)
    {
        for(std::vector<Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
            if (it->getFd() == *remove_it)
            {
                close(it->getFd());
                _clients.erase(it);
                break;
            }
        for(std::vector<struct pollfd>::iterator it = _pollfds.begin(); it != _pollfds.end(); ++it)
            if (it->fd == *remove_it)
            {
                _pollfds.erase(it);
                break;
            }
    }
    _removeClients.clear();
}

void Server::handleRead(Client &client)
{
    char    buffer[4096];
    Request req;
    bool    header_status;

    ssize_t bytes = recv(client.getFd(), buffer, sizeof(buffer), 0);
    if (bytes > 0)
    {
        client.appendRequest(buffer, bytes);
        /*==============================Request complete & RequestParser======================*/
        header_status = req.checkHeader(client.getRequestBuffer());

        if (req.getHasContentStatus() && req.getTransferEncodingStatus())
            throw std::runtime_error("Invalid request format");
    
        if (header_status && req.getHasContentStatus())
        {
            req.readingBody(client.getRequestBuffer());

            if (req.checkingBody_framing(client.getRequestBuffer()))
            {
                std::cout << "Framing complete" << std::endl;
                req.start_parsing(client.getRequestBuffer());
            }
            else
                std::cout << "Framing not complete" << std::endl;
        }
        else if (header_status && req.getTransferEncodingStatus())
        {
            req.readingBody(client.getRequestBuffer());

            if (req.checkingBody_chuncked(client.getRequestBuffer()))
            {
                std::cout << "Chuncked complete" << std::endl;
                req.start_parsing(client.getRequestBuffer());
            }
            else
                std::cout << "Chuncked not complete" << std::endl;
        }
        else if (!header_status)
            std::cout << "Request not complete" << std::endl;
        else
        {
            std::cout << "Request (no body) complete" << std::endl;
            req.start_parsing(client.getRequestBuffer());
        }
        /*==============================================================================*/
        std::cout << bytes << "bytes received\n";
        for (std::vector<ServerConfig>::iterator it = client.getListener().configs.begin(); it != client.getListener().configs.end(); ++it)
        {
            for (std::vector<std::string>::iterator sn_it = it->getServerName().begin(); sn_it != it->getServerName().end(); ++sn_it)
            {
                if (*sn_it == "Host") // trocar o "host" pelo o host que e recebido no request
                {
                    client.setConfig(*it);
                    break;
                } 
            }
        }
    }
    else if (bytes == 0)
        _removeClients.push_back(client.getFd());
    else
        std::cout << "Error at request reading\n";
}

void Server::handleWrite(Client &client)
{
    const std::string &res = client.getResponseBuffer();
    size_t offset = client.getResponseOffset();
    ssize_t bytes = send(client.getFd(), res.c_str() + offset, res.length() - offset, 0);
    if (bytes == -1)
        throw(std::runtime_error("Error on send()"));
    client.setResponseOffset(offset + bytes);
    if (client.getResponseOffset() == res.size())
        Server::disablePollOut(client.getFd());
}

void Server::disablePollOut(int fd)
{
    for (size_t i = 0; i < _pollfds.size(); i++)
    {
        if (_pollfds[i].fd == fd)
        {
            _pollfds[i].events &= ~POLLOUT;
            break ;
        }
    }
}

void Server::acceptClient(int fd)
{
    sockaddr_in client_addr;
    socklen_t   client_addr_len = sizeof(client_addr);

    int client_fd = accept(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);

    if (client_fd == -1)
        throw(std::runtime_error("Error on accept()"));

    if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1)
    {
        close(client_fd);
        throw(std::runtime_error("Error on fcntl()"));
    }
    Listener listener;
    for (std::vector<Listener>::iterator it = _listeners.begin(); it != _listeners.end(); ++it)
    {
        if (it->fd == fd)
        {
            listener = *it;
            break;
        }
    }

    this->_clients.push_back(Client(client_fd, listener));

    struct pollfd pfd;

    pfd.fd = client_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    this->_pollfds.push_back(pfd);
    std::cout << "clients: " << _clients.size() << std::endl;
    std::cout << "poll: " << _pollfds.size() << std::endl;

    std::cout << "Client created!" << std::endl;
}

Client* Server::getClientById(int fd)
{
    for (std::vector<Client>::iterator it = _clients.begin(); it != _clients.end(); it++)
    {
        if (it->getFd() == fd)
            return &(*it);
    }
    return (NULL);
}

Listener* Server::FindListener(int port)
{
    for (std::vector<Listener>::iterator it = _listeners.begin(); it != _listeners.end(); ++it)
    {
        if (it->port == port)
            return &(*it);
    }
    return (NULL);
}

void Server::shut_down() 
{ 
    for (std::vector<Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
    {
        if (it->getFd() != -1)
            close (it->getFd());
    }
    this->_clients.clear();
    
    for (std::vector<struct pollfd>::iterator it = _pollfds.begin(); it != _pollfds.end(); ++it)
    {
        if (it->fd != -1)
            close (it->fd);
    }
    this->_pollfds.clear();

    for (std::vector<int>::iterator it = _server_fds.begin(); it != _server_fds.end(); ++it)
    {
        if (*it != -1)
            close(*it);
    }
    this->_server_fds.clear();
}
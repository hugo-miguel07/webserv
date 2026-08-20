#include "Server.hpp"
#include "Client.hpp"

Server::Server(int port) : _server_fd(-1), _port(port) {}

Server::~Server() { this->shut_down(); }

bool Server::init() 
{
    this->_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->_server_fd == -1)
    {
        std::cout << "Error on socket()" << std::endl;
        return false;
    }

    if (fcntl(_server_fd, F_SETFL, O_NONBLOCK) == -1)
    {
        std::cout << "Error on fnctl()\n";
        close(_server_fd);
        return false ;
    }

    sockaddr_in address;
    
    address.sin_family = AF_INET;
    address.sin_port = htons(this->_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(this->_server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
    {
        close(this->_server_fd);
        std::cout << "Error on bind()" << std::endl;
        return false;
    }
    if (listen(this->_server_fd, SOMAXCONN) == -1)
    {
        close(this->_server_fd);
        std::cout << "Error on listen()" << std::endl;
        return false;
    }
    struct pollfd pfd;

    pfd.fd = _server_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    _pollfds.push_back(pfd);
    return (true);
}

void Server::enablePollOut()
{
    for (size_t i = 0; i < _pollfds.size(); i++)
    {
        if (_pollfds[i].fd == _server_fd)
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
            std::cout << "Error on poll()\n";
            break ;
        }

        size_t size = _pollfds.size();
        for (size_t i = 0; i < size; i++)
        {
            if (_pollfds[i].revents & POLLIN)
            {  
                if (_pollfds[i].fd == _server_fd)
                    Server::acceptClient();
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

int Server::getServerFd() const {return (this->_server_fd); }

void Server::removeClients()
{
    if (_removeClients.empty())
        return ;
    for(std::vector<Client>::iterator remove_it = _removeClients.begin(); remove_it != _removeClients.end(); ++remove_it)
    {
        for(std::vector<Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
            if (it->get_fd() == remove_it->get_fd())
            {
                close(it->get_fd());
                _clients.erase(it);
                break;
            }
        for(std::vector<struct pollfd>::iterator it = _pollfds.begin(); it != _pollfds.end(); ++it)
            if (it->fd == remove_it->get_fd())
            {
                _pollfds.erase(it);
                break;
            }
    }
    _removeClients.clear();
}

void Server::handleRead(Client &client)
{
    char buffer[4096];

    ssize_t bytes = recv(client.get_fd(), buffer, sizeof(buffer), 0);
    if (bytes > 0)
    {
        client.appendRequest(buffer, bytes);
        client.mirror();
        client.setResponseReady(true);
        std::cout << bytes << "bytes received\n";
    }
    else if (bytes == 0)
        _removeClients.push_back(client.get_fd());
    else
        std::cout << "Error at request reading\n";
}

void Server::handleWrite(Client &client)
{
    const std::string &res = client.getResponseBuffer();
    size_t offset = client.getResponseOffset();
    ssize_t bytes = send(client.get_fd(), res.c_str() + offset, res.length() - offset, 0);
    if (bytes == -1)
    {
        std::cout << "Error on send()\n";
        return ;
    }
    client.setResponseOffset(offset + bytes);
    if (client.getResponseOffset() == res.size())
        Server::disablePollOut(client.get_fd());
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

void Server::acceptClient()
{
    sockaddr_in client_addr;
    socklen_t   client_addr_len = sizeof(client_addr);

    int client_fd = accept(this->_server_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);

    if (client_fd == -1)
    {
        std::cout << "Error on accept()" << std::endl;
        return ;    
    }

    if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1)
    {
        std::cout << "Error on fnctl()\n";
        close(client_fd);
        return ;
    }

    this->_clients.push_back(Client(client_fd));

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
        if (it->get_fd() == fd)
            return &(*it);
    }
    return (NULL);
}

void Server::shut_down() 
{ 
    for (std::vector<Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
    {
        if (it->get_fd() != -1)
            close (it->get_fd());
    }
    this->_clients.clear();
    
    for (std::vector<struct pollfd>::iterator it = _pollfds.begin(); it != _pollfds.end(); ++it)
    {
        if (it->fd != -1)
            close (it->fd);
    }
    this->_pollfds.clear();

    if (this->_server_fd != -1)
        close(this->_server_fd); 
}

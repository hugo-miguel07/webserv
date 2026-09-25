#include "Server.hpp"
#include "Client.hpp"
#include "Request.hpp"

Server::Server(const Config &config) : _config(config) {}

Server::~Server() { this->shut_down(); }

bool Server::init() 
{
    for (std::vector<ServerConfig>::iterator it = _config.getServers().begin(); it != _config.getServers().end(); ++it)
    {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
        {
            std::cout << "Error on socket()" << std::endl;
            return false;
        }

        if (fcntl(server_fd, F_SETFL, O_NONBLOCK) == -1)
        {
            std::cout << "Error on fnctl()\n";
            close(server_fd);
            return false ;
        }

        sockaddr_in address;

        address.sin_family = AF_INET;
        address.sin_port = htons(it->getPort());
        address.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
        {
            close(server_fd);
            std::cout << "Error on bind()" << std::endl;
            return false;
        }
        if (listen(server_fd, SOMAXCONN) == -1)
        {
            close(server_fd);
            std::cout << "Error on listen()" << std::endl;
            return false;
        }
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
    for (std::vector<int>::iterator it = _server_fds.begin();
         it != _server_fds.end();
         ++it)
    {
        if (*it == fd)
            return true;
    }
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
            std::cout << "Error on poll()\n";
            break ;
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
                    {
                        try{
                            Server::handleRead(*client);
                        }
                        catch (const std::exception &e) {
                            std::cerr << "Exception: " << e.what() << std::endl;
                        }
                    }
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
    Request req;
    bool    header_status;

    ssize_t bytes = recv(client.get_fd(), buffer, sizeof(buffer), 0);
    if (bytes > 0)
    {
        client.appendRequest(buffer, bytes);
        //

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

void Server::acceptClient(int fd)
{
    sockaddr_in client_addr;
    socklen_t   client_addr_len = sizeof(client_addr);

    int client_fd = accept(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);

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

    for (size_t i = 0; _server_fds[i]; i++)
        if (this->_server_fds[i] != -1)
            close(this->_server_fds[i]); 
}
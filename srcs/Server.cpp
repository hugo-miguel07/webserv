#include "Server.hpp"
#include "Client.hpp"
#include "Request.hpp"
#include "StaticHandler.hpp"
#include "UploadHandler.hpp"
#include "DeleteHandler.hpp"
#include "RedirectHandler.hpp"
#include "CGIHandler.hpp"
#include <cerrno>

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

Locations Server::findLocation(const ServerConfig &config, const std::string &path) const
{
    std::vector<Locations> locations = config.getLocations();
    size_t best = locations.size();
    for (size_t i = 0; i < locations.size(); ++i)
    {
        const std::string prefix = locations[i].getPath();
        if (prefix.empty() || path.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (path.size() != prefix.size() && prefix[prefix.size() - 1] != '/'
            && path[prefix.size()] != '/')
            continue;
        if (best == locations.size() || prefix.size() > locations[best].getPath().size())
            best = i;
    }
    if (best == locations.size())
        throw 404;
    return locations[best];
}

void Server::enablePollOut()
{
    for (size_t i = 0; i < _pollfds.size(); i++)
    {
        if (isServerFd(_pollfds[i].fd))
            continue;
        Client *client = getClientById(_pollfds[i].fd);
        if (client && client->getResponseReady())
            _pollfds[i].events = POLLOUT;
        else
            _pollfds[i].events = POLLIN;
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
    if (client.getResponseReady())
    {
        return;
    }

    char read_buffer[4096];
    const ssize_t received_bytes = recv(client.getFd(), read_buffer, sizeof(read_buffer), 0);

    if (received_bytes == 0)
    {
        _removeClients.push_back(client.getFd());
        return;
    }

    if (received_bytes < 0)
    {
        const int read_error = errno;
        const bool can_retry = read_error == EAGAIN
            || read_error == EWOULDBLOCK || read_error == EINTR;

        if (!can_retry)
        {
            _removeClients.push_back(client.getFd());
        }
        return;
    }

    client.appendRequest(read_buffer, received_bytes);

    Request request(client.getListener().configs);
    Response response;
    bool request_parsed = false;

    try
    {
        const std::string &request_buffer = client.getRequestBuffer();
        const bool headers_complete = request.checkHeader(request_buffer);
        if (!headers_complete)
        {
            return;
        }

        request.HeadersToMap(request_buffer);
        request.readingBody(request_buffer);

        const ServerConfig &server_config = request.getServerConfig();
        const size_t max_body_size = server_config.getBodySize();
        if (request.getContentLength() > max_body_size)
        {
            throw 413;
        }

        if (request.getHasContentStatus())
        {
            const bool body_complete = request.checkingBody_framing(request_buffer);
            if (!body_complete)
            {
                return;
            }
        }

        if (request.getTransferEncodingStatus())
        {
            const bool body_complete = request.checkingBody_chuncked(request_buffer);
            if (!body_complete)
            {
                return;
            }
        }

        request.start_parsing(request_buffer);
        request_parsed = true;

        if (request.body.size() > max_body_size)
        {
            throw 413;
        }

        const Locations location = findLocation(server_config, request.path);

        StaticHandler static_handler;
        RedirectHandler redirect_handler;
        CGIHandler cgi_handler;
        UploadHandler upload_handler;
        DeleteHandler delete_handler;
        Handler *selected_handler = &static_handler;

        if (!location.getReturn().empty())
        {
            selected_handler = &redirect_handler;
        }
        else if (!location.getCgiExtension().empty() || !location.getCgiPath().empty())
        {
            selected_handler = &cgi_handler;
        }
        else if (request.method == "POST" && !location.getUploadStore().empty())
        {
            selected_handler = &upload_handler;
        }
        else if (request.method == "DELETE")
        {
            selected_handler = &delete_handler;
        }

        const Handler::e_status handler_status =
            selected_handler->handle(request, response, server_config, location);

        const bool handler_finished = handler_status == Handler::DONE;
        const bool response_has_error = response.statusCode >= 400;
        if (!handler_finished && !response_has_error)
        {
            response.clear();
            response.setError(500);
        }
    }
    catch (int status_code)
    {
        response.clear();
        response.setError(status_code);

        if (status_code == 405)
        {
            const Locations location = findLocation(request.getServerConfig(), request.path);
            const std::vector<std::string> allowed_methods = location.getAllowedMethods();
            std::string allow_header;

            for (size_t i = 0; i < allowed_methods.size(); ++i)
            {
                if (i > 0)
                {
                    allow_header += ", ";
                }
                allow_header += allowed_methods[i];
            }

            response.setHeader("Allow", allow_header);
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Request error: " << error.what() << std::endl;
        response.clear();

        int status_code = 400;
        if (request_parsed)
        {
            status_code = 500;
        }
        response.setError(status_code);
    }

    response.setHeader("Connection", "close");
    const std::string serialized_response = response.toString();
    client.appendResponse(serialized_response.data(), serialized_response.size());
    client.setResponseOffset(0);
    client.setResponseReady(true);
}

void Server::handleWrite(Client &client)
{
    const std::string &res = client.getResponseBuffer();
    size_t offset = client.getResponseOffset();
    ssize_t bytes = send(client.getFd(), res.c_str() + offset, res.length() - offset, MSG_NOSIGNAL);
    if (bytes <= 0)
    {
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        client.setResponseReady(false);
        _removeClients.push_back(client.getFd());
        return;
    }
    client.setResponseOffset(offset + bytes);
    if (client.getResponseOffset() == res.size())
    {
        client.setResponseReady(false);
        Server::disablePollOut(client.getFd());
        _removeClients.push_back(client.getFd());
    }
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
    Listener *listener = NULL;
    for (std::vector<Listener>::iterator it = _listeners.begin(); it != _listeners.end(); ++it)
    {
        if (it->fd == fd)
        {
            listener = &(*it);
            break;
        }
    }
    if (!listener)
        throw std::runtime_error("Listener not found");

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
        {
            close (it->getFd());
            it->setFd(-1);
        }
    }
    this->_clients.clear();
    
    for (std::vector<struct pollfd>::iterator it = _pollfds.begin(); it != _pollfds.end(); ++it)
    {
        if (it->fd != -1)
        {
            close (it->fd);
            it->fd = -1;
        }
    }
    this->_pollfds.clear();

    for (std::vector<int>::iterator it = _server_fds.begin(); it != _server_fds.end(); ++it)
    {
        if (*it != -1)
        {
            close(*it);
            *it = -1;
        }
    }
    this->_server_fds.clear();
}

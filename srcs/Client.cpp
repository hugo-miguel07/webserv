#include "../includes/Client.hpp"
#include "../includes/Request.hpp"

Client::Client() : _fd(-1), _responseOffset(0), _responseReady(false), _listener(NULL), _config(NULL) {}

Client::Client(int client_fd, Listener *listener) : _fd(client_fd), _responseOffset(0), _responseReady(false), _listener(listener), _config(NULL) {}

Client::~Client() {}

int Client::getFd() const { return (this->_fd); }

void Client::setFd(int fd)  { (_fd = fd); }

const Listener& Client::getListener() const { return (*this->_listener); }

void Client::appendRequest(const char *buffer, int total_bytes)
{
    if (!buffer)
        return;
    this->_requestBuffer.append(buffer, total_bytes);
}

void Client::appendResponse(const char *buffer, int total_bytes)
{
    if (!buffer)
        return;
    this->_responseBuffer.append(buffer, total_bytes);
}

const std::string& Client::getResponseBuffer() const
{
    return (_responseBuffer);
}

const std::string& Client::getRequestBuffer() const
{
    return (_requestBuffer);
}

size_t Client::getResponseOffset() const
{
    return (_responseOffset);
}

void Client::setResponseOffset(size_t value)
{
    _responseOffset = value;
}

const ServerConfig& Client::getConfig() const
{
    return (*_config);
}

bool Client::getResponseReady() const
{
    return (_responseReady);
}

void Client::setResponseReady(bool flag)
{
    _responseReady = flag;
}

void Client::setConfig(const ServerConfig& config)
{
    _config = &config;
}




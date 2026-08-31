#include "../includes/Client.hpp"
#include "../includes/Request.hpp"

Client::Client(){}

Client::Client(int client_fd) : _fd(client_fd), _responseOffset(0), _responseReady(false) {}

Client::~Client() {}

int Client::get_fd() const { return (this->_fd); }

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

bool Client::getResponseReady() const
{
    return (_responseReady);
}

void Client::setResponseReady(bool flag)
{
    _responseReady = flag;
}

void Client::mirror()
{
    _responseBuffer = _requestBuffer;
}



#include "../includes/Request.hpp"

Request::Request() : Client(){}

Request::~Request(){}

bool    Request::checkHeader(const std::string buffer)
{

    if (buffer.find("Content-Length") != std::string::npos)
        _hasContent = true;

    if (buffer.find("Transfer-Encoding") != std::string::npos)
        _hasTransferEncoding = true;

    else if (buffer.find("\r\n\r\n") != std::string::npos){
        _finisedHeader = true;

        return true;
    }
    else
        return false;
}

void Request::readingBody(const std::string buffer)
{
    if (_hasContent)
    {
        std::string content = "Content-Length: ";

        size_t start = buffer.find(content);

        start += content.size();

        size_t end = buffer.find("\r\n", start);

        std::string value = buffer.substr(start, end - start);

        std::istringstream(value) >> _contentLen;
    }

    else if (_hasTransferEncoding)
    {
        std::string method = "Transfer-Encoding: ";

        size_t start = buffer.find(method);

        start += method.size();

        size_t end = buffer.find("\r\n", start);

        std::string value = buffer.substr(start, end - start);

        if (value != "chunked")
            std::cout << "Invalid transfer encoding" << std::endl;

        std::istringstream(value) >> _TransferMethod;
    }

}

bool Request::checkBody(const std::string buffer)
{
    std::string header_end = "\r\n\r\n";

    size_t body_start = buffer.find("\r\n\r\n");
    
    body_start += header_end.size();

    size_t body_bytes = buffer.size() - body_start;

    if (body_bytes < _contentLen)
        return false;
    if (body_bytes == _contentLen)
        return true;
    else
    {
        _nextRequestBytes = body_bytes - _contentLen;
        return true;
    }
}
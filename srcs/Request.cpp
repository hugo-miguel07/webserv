#include "../includes/Request.hpp"

Request::Request() : Client(), _TransferMethod(""), 
            _nextRequestBytes(0), 
            _contentLen(0), 
            _finisedHeader(false), 
            _hasContent(false), 
            _hasTransferEncoding(false){}

Request::~Request(){}

bool    Request::getHasContentStatus(){
    return _hasContent;
}

bool    Request::getTransferEncodingStatus(){
    return _hasTransferEncoding;
}

bool    Request::checkHeader(const std::string buffer)
{

    if (buffer.find("Content-Length") != std::string::npos){
        this->_hasContent = true;
        std::cout << "Found content " << getHasContentStatus() << std::endl;
    }

    if (buffer.find("Transfer-Encoding") != std::string::npos){
        this->_hasTransferEncoding = true;
        std::cout << "Found enconder " << getTransferEncodingStatus() << std::endl;
    }

    else if (buffer.find("\r\n\r\n") != std::string::npos){
        _finisedHeader = true;

        return true;
    }
    else
        return false;
    return true;
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

        for (size_t i = 0; i < value.size(); i++){

            if (!std::isdigit(static_cast<unsigned char>(value[i])))
                throw std::runtime_error("Invalid content len");
        }

        std::istringstream val_stream(value);
        val_stream >> _contentLen;
    }

    else if (_hasTransferEncoding)
    {
        std::string method = "Transfer-Encoding: ";

        size_t start = buffer.find(method);

        start += method.size();

        size_t end = buffer.find("\r\n", start);

        std::string value = buffer.substr(start, end - start);

        if (value != "chunked")
            throw std::runtime_error("Invalid encoding information");

        std::istringstream transfer_stream(value);
        transfer_stream >> _TransferMethod;
    }

}

bool Request::checkingBody_framing(const std::string buffer)
{
    /*==========Extracting body==========*/

    std::string header_end = "\r\n\r\n";

    size_t body_start = buffer.find("\r\n\r\n");
    
    body_start += header_end.size();
    /*=================================*/

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

bool Request::checkingBody_chuncked(const std::string buffer)
{
    //std::cout << "starting checking chuncks" << std::endl;
    std::string line;

    /*==============Extracting body=================*/

    std::string header_end = "\r\n\r\n";

    size_t body_start = buffer.find("\r\n\r\n");
    
    body_start += header_end.size();
    /*=======================*/

    //std::cout << "Transfer method: " << _TransferMethod << std::endl;
    while (true)
    {
        size_t end = buffer.find("\r\n", body_start);

        if (end == std::string::npos)
            return false;

        std::string size_str = buffer.substr(body_start, end - body_start);

        unsigned long chunk_size;

        std::istringstream(size_str) >> std::hex >> chunk_size;

        body_start = end + 2;

        if (chunk_size == 0)
            return true;
        
        if (buffer.size() < body_start + chunk_size + 2)
            return false;

        body_start += chunk_size;

        if (buffer.substr(body_start, 2) != "\r\n")
            throw std::runtime_error("Invalid chunk");

        body_start += 2;
    }
}
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

bool Request::checkHeader(const std::string buffer)
{
    if (buffer.find("Content-Length") != std::string::npos)
        _hasContent = true;

    if (buffer.find("Transfer-Encoding") != std::string::npos)
        _hasTransferEncoding = true;

    if (buffer.find("\r\n\r\n") == std::string::npos)
        return false;

    _finisedHeader = true;
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

        if (value.empty())
            throw std::runtime_error("Invalid content len");
    
        std::istringstream val_stream(value);
        val_stream >> _contentLen;

        if (val_stream.fail())
            throw std::runtime_error("Invalid content len");
    }

    else if (_hasTransferEncoding)
    {
        std::string method = "Transfer-Encoding: ";

        size_t start = buffer.find(method);

        start += method.size();

        size_t end = buffer.find("\r\n", start);

        std::string value = buffer.substr(start, end - start);

        if (value != "chunked" || value.empty())
            throw std::runtime_error("Invalid encoding information");

        std::istringstream transfer_stream(value);
        transfer_stream >> _TransferMethod;

        if (transfer_stream.fail())
            throw std::runtime_error("Invalid content len");
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
        std::cout << "too many bytes" << std::endl;
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
        {
            if (buffer.size() < body_start + 2)
                return false;

            if (buffer.substr(body_start, 2) != "\r\n")
                throw std::runtime_error("Invalid chunk ending");

            return true;
        }
        
        if (buffer.size() < body_start + chunk_size + 2)
            return false;

        body_start += chunk_size;

        if (buffer.substr(body_start, 2) != "\r\n")
            throw std::runtime_error("Invalid chunk");

        body_start += 2;
    }
}

void    Request::start_parsing(const std::string requestbuffer)
{
    //std::cout << "About to parse" << std::endl; 
    RequestLineParsing(requestbuffer);
    HeadersToMap(requestbuffer);
    //BodyParsing(requestbuffer);

    for (std::map<std::string, std::string>::iterator it = _requestLine.begin(); it != _requestLine.end(); it++){
        std::cout << it->first << ":" << it->second << std::endl;
    }

    std::cout << std::endl;
    for (std::map<std::string, std::string>::iterator it = _headers.begin(); it != _headers.end(); it++){
        std::cout << it->first << ":" << it->second << std::endl;
    }

    std::cout << "Parsing done" << std::endl; 
}

void    Request::RequestLineParsing(const std::string request)
{
    bool hasQuery = false;

    size_t request_line_end = request.find("\r\n");
    if (request_line_end == std::string::npos)
        throw std::runtime_error("Invalid request line(invalid end)!");

    std::string request_line = request.substr(0, request_line_end);
    if (request_line.empty())
        throw std::runtime_error("Invalid request line!");

    /*==========Getting method================*/

    size_t method_end = request_line.find(" ");
    if (method_end == std::string::npos)
        throw std::runtime_error("Invalid request line (no method)");
    
    std::string method = request_line.substr(0, method_end);
    if (method.empty())
        throw std::runtime_error("Method doesnt exist!");

    if (method != "GET" && method != "POST" && method != "DELETE")
        throw std::runtime_error("Invalid method");
    
    MethodParsing(method);
    _requestLine.insert(std::make_pair("Method", method));

    /*================Request target===============*/

    size_t target_start = method_end + 1;
    size_t target_end = request_line.find(" ", target_start);

    if (target_end == std::string::npos)
        throw std::runtime_error("Invalid request target(doesnt exixt)");

    std::string request_target = request_line.substr(
        target_start, 
        target_end - target_start);
    
    if (request_target.empty())
        throw std::runtime_error("Inavalid request target");
    
    if (request_target[0] != '/')
        throw std::runtime_error("Invalid request target");
    
    if (request_target.find('?') != std::string::npos)
        hasQuery = true;

    /*===========HTTP version 1.1===================*/

    size_t version_start = target_end + 1;

    std::string version = request_line.substr(version_start);

    if (version != "HTTP/1.1")
        throw std::runtime_error("Invalid HTTP version");

    _requestLine.insert(std::make_pair("Version", version));

    if (hasQuery == true)
        QueryParsing(request_target);
    else
        _requestLine.insert(std::make_pair("Path", request_target));


}

void Request::QueryParsing(const std::string request_target)
{
    size_t query_pos = request_target.find('?');

    if (query_pos == std::string::npos)
        throw std::runtime_error("Query not found");

    std::string path = request_target.substr(0, query_pos);
    std::string query = request_target.substr(query_pos + 1);

    if (path.empty())
        throw std::runtime_error("Invalid path");

    if (query.empty())
        throw std::runtime_error("Invalid query");

    _requestLine.insert(std::make_pair("Path", path));
    _requestLine.insert(std::make_pair("Query", query));
}

void    Request::HeadersToMap(const std::string request)
{
    std::istringstream stream(request);
    std::string line;

    std::getline(stream, line);

    while (std::getline(stream, line)){
        if ( line == "\r")
            break;
        
        size_t colon = line.find(':');

        if (colon == std::string::npos)
            throw std::runtime_error("Invalid header");

        std::string key = line.substr(0, colon);

        if (key.empty())
            throw std::runtime_error("Invalid header name");

        std::string value = line.substr(colon + 1);

        value = trim(value);

        _headers.insert(std::make_pair(key, value));
    }
    //HostParsing();

}

void    Request::HostParsing()
{
     
}

void    Request::MethodParsing(std::string method)
{
    std::cout << method << std::endl;


    /*verifcar no aloowed methods*/
    if (method != "GET" && method != "POST" && method != "DELETE")
        throw std::runtime_error("Invalid http method");
}
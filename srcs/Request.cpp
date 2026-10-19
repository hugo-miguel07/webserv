#include "Request.hpp"
#include "Locations.hpp"
#include "Config.hpp"

static std::string lowerHeaderName(const std::string& name)
{
    std::string result = name;

    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    return result;
}

static bool validHeaderName(const std::string& name)
{
    if (name.empty())
        return false;

    for (size_t i = 0; i < name.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c) != 0 || name[i] == '!' || name[i] == '#'
            || name[i] == '$' || name[i] == '%' || name[i] == '&'
            || name[i] == '\'' || name[i] == '*' || name[i] == '+'
            || name[i] == '-' || name[i] == '.' || name[i] == '^'
            || name[i] == '_' || name[i] == '`' || name[i] == '|'
            || name[i] == '~')
            continue;
        return false;
    }
    return true;
}

static std::string getHeaderValue(const std::string& buffer,
                                  const std::string& headerName)
{
    size_t headersEnd = buffer.find("\r\n\r\n");
    if (headersEnd == std::string::npos)
        return "";

    size_t lineStart = buffer.find("\r\n");
    while (lineStart != std::string::npos && lineStart < headersEnd)
    {
        lineStart += 2;
        size_t lineEnd = buffer.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > headersEnd)
            lineEnd = headersEnd;

        size_t colon = buffer.find(':', lineStart);
        if (colon != std::string::npos && colon < lineEnd)
        {
            std::string name = buffer.substr(lineStart, colon - lineStart);
            if (lowerHeaderName(name) == headerName)
                return trim(buffer.substr(colon + 1, lineEnd - colon - 1));
        }

        if (lineEnd == headersEnd)
            break;
        lineStart = lineEnd;
    }
    return "";
}

Request::Request() : Client(), _TransferMethod(""), 
            _nextRequestBytes(0), 
            _contentLen(0), 
            _finisedHeader(false), 
            _hasContent(false), 
            _hasTransferEncoding(false),
            _client(NULL){}

Request::Request(const Client& client) : Client(client), _TransferMethod(""),
            _nextRequestBytes(0),
            _contentLen(0),
            _finisedHeader(false),
            _hasContent(false),
            _hasTransferEncoding(false),
            _client(&client)
{
    _parsedServers = client.getListener().configs;
}


Request::Request(std::vector<ServerConfig>   parsedServers) : Client(), _TransferMethod(""),
            _nextRequestBytes(0), 
            _contentLen(0), 
            _finisedHeader(false), 
            _hasContent(false), 
            _hasTransferEncoding(false),
            _parsedServers(parsedServers),
            _client(NULL){}

Request::~Request(){}

bool    Request::getHasContentStatus(){
    return _hasContent;
}

bool    Request::getTransferEncodingStatus(){
    return _hasTransferEncoding;
}

bool Request::checkHeader(const std::string buffer)
{
    if (buffer.find("\r\n\r\n") == std::string::npos)
        return false;

    _hasContent = !getHeaderValue(buffer, "content-length").empty();
    _hasTransferEncoding = !getHeaderValue(buffer, "transfer-encoding").empty();

    _finisedHeader = true;
    return true;
}

void Request::readingBody(const std::string buffer)
{
    if (_hasContent)
    {
        std::string value = getHeaderValue(buffer, "content-length");

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
        std::string value = getHeaderValue(buffer, "transfer-encoding");

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
    std::string header_end = "\r\n\r\n";

    size_t body_start = buffer.find(header_end);

    if (body_start == std::string::npos)
        return false;

    body_start += header_end.size();

    std::cout << "Transfer method: "
              << _TransferMethod
              << std::endl;

    while (true)
    {
        size_t end = buffer.find("\r\n", body_start);

        if (end == std::string::npos)
            return false;

        std::string size_str =
            buffer.substr(body_start, end - body_start);

        if (size_str.empty())
            throw std::runtime_error("Invalid chunk size");

        unsigned long chunk_size;

        std::istringstream stream(size_str);
        stream >> std::hex >> chunk_size;

        if (stream.fail())
            throw std::runtime_error("Invalid chunk size");

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
    MethodParsing(_requestLine["Method"]);
    readingBody(requestbuffer);
    parseBody(requestbuffer);

    method = _requestLine["Method"];
    path = _requestLine["Path"];
    query = _requestLine["Query"];
    body = _body;
    headers = _headers;

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

    if (method != "GET" && method != "POST" && method != "DELETE" && method != "HEAD")
        throw std::runtime_error("Invalid method");
    
    _requestLine.insert(std::make_pair("Method", method));
    std::cout << "Method " << method << std::endl;

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

    std::cout << "Target " << request_target << std::endl;

    /*===========HTTP version 1.1===================*/

    size_t version_start = target_end + 1;

    std::string version = request_line.substr(version_start);

    if (version != "HTTP/1.1")
        throw std::runtime_error("Invalid HTTP version");

    _requestLine.insert(std::make_pair("Version", version));

    request_target = trim(request_target);

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
    std::cout << "Query " << path << std::endl;
    std::cout << "Path " << query << std::endl; 
}

void    Request::HeadersToMap(const std::string request)
{
    std::istringstream stream(request);
    std::string line;
    bool found_host = false;

    std::getline(stream, line);

    while (std::getline(stream, line)){
        if ( line == "\r")
            break;
        
        size_t colon = line.find(':');

        if (colon == std::string::npos)
            throw std::runtime_error("Invalid header");

        std::string key = lowerHeaderName(trim(line.substr(0, colon)));

        
        if (!validHeaderName(key))
        throw std::runtime_error("Invalid header name");
    
        std::string value = line.substr(colon + 1);

        if (key == "host")
        {
            found_host = true;
            HostParsing(value);
        }

        value = trim(value);

        _headers.insert(std::make_pair(key, value));
    }

    if (!found_host)
        throw std::runtime_error("No host");
}

void Request::HostParsing(std::string host)
{
    host = trim(host);

    std::vector<ServerConfig> servers = _parsedServers;

    for (size_t server = 0; server < servers.size(); ++server)
    {
        std::vector<std::string> names = servers[server].getServerName();

        for (size_t i = 0; i < names.size(); ++i)
        {
            std::string value = names[i];

            size_t colon = host.find(':');
            if (colon != std::string::npos)
                host = host.substr(0, colon);
        
            if (host == value){
                std::cout << "Host " << host << std::endl;
                _server = servers[server];
                _serverName = value;
                if (_client)
                    setConfig(_client->getListener().configs[server]);
                else
                    setConfig(_server);
                return;
            }
        }
    }

    throw std::runtime_error("Invalid host");
}

void Request::parseBody(const std::string& buffer)
{
    size_t header_end = buffer.find("\r\n\r\n");

    if (header_end == std::string::npos)
        throw std::runtime_error("Invalid body");

    size_t body_start = header_end + 4;

    if (_hasContent)
    {
        _body = buffer.substr(body_start, _contentLen);
        std::cout << "Body " << _body << std::endl;
    }
    else if (_hasTransferEncoding)
    {
        parseChunkedBody(buffer, body_start);
    }
    else
        _body.clear();
    return ;
}

void Request::MethodParsing(std::string method)
{
    std::string path = _requestLine["Path"];

    std::vector<Locations> locations =
        _server.getLocations();
    size_t selected = 0;
    size_t selectedLength = 0;
    bool found = false;

    for (size_t i = 0; i < locations.size(); ++i)
    {
        std::string locationPath = locations[i].getPath();
        bool prefix = path.find(locationPath) == 0;
        bool boundary = locationPath == "/"
            || path.size() == locationPath.size()
            || (path.size() > locationPath.size()
                && path[locationPath.size()] == '/');

        if (prefix && boundary && locationPath.size() > selectedLength)
        {
            selected = i;
            selectedLength = locationPath.size();
            found = true;
        }
    }

    if (!found)
        throw std::runtime_error("Location not found");

    std::vector<std::string> allowed_methods =
        locations[selected].getAllowedMethods();

    for (size_t j = 0; j < allowed_methods.size(); ++j)
    {
        if (method == allowed_methods[j])
            return;
    }

    throw std::runtime_error("Method not allowed");
}

void Request::parseChunkedBody(const std::string& buffer, size_t pos)
{
    _body.clear();

    while (true)
    {
        size_t end = buffer.find("\r\n", pos);

        if (end == std::string::npos)
            throw std::runtime_error("Invalid chunk");

        std::string size_str = buffer.substr(pos, end - pos);

        unsigned long chunk_size;
        std::istringstream stream(size_str);

        stream >> std::hex >> chunk_size;

        if (stream.fail())
            throw std::runtime_error("Invalid chunk size");

        pos = end + 2;

        if (chunk_size == 0)
            break;

        _body.append(buffer, pos, chunk_size);

        pos += chunk_size;

        if (buffer.substr(pos, 2) != "\r\n")
            throw std::runtime_error("Invalid chunk");

        pos += 2;
    }
    std::cout << "Chuncked Body " << _body << std::endl;
}
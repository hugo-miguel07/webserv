#include "Request.hpp"
#include <cctype>
#include <arpa/inet.h>
#include <limits>

namespace
{
    std::string lowerCase(std::string value)
    {
        for (size_t i = 0; i < value.size(); ++i)
            value[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(value[i])));
        return value;
    }

    std::string canonicalHost(std::string value)
    {
        value = lowerCase(value);
        if (!value.empty() && value[value.size() - 1] == '.')
            value.erase(value.size() - 1);
        return value;
    }

    bool isTokenChar(unsigned char c)
    {
        return std::isalnum(c)
            || std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos;
    }

    void parseHeader(const std::string& line, std::string& key,
        std::string& value)
    {
        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0)
            throw std::runtime_error("Invalid header");
        key = line.substr(0, colon);
        for (size_t i = 0; i < key.size(); ++i)
            if (!isTokenChar(static_cast<unsigned char>(key[i])))
                throw std::runtime_error("Invalid header name");
        key = lowerCase(key);
        value = line.substr(colon + 1);
        for (size_t i = 0; i < value.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(value[i]);
            if ((c < 32 && c != '\t') || c == 127)
                throw std::runtime_error("Invalid header value");
        }
        value = trim(value);
    }

    size_t parseSize(const std::string& value, unsigned int base)
    {
        if (value.empty())
            throw std::runtime_error("Invalid body size");
        size_t result = 0;
        for (size_t i = 0; i < value.size(); ++i)
        {
            unsigned int digit;
            unsigned char c = static_cast<unsigned char>(value[i]);
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (base == 16 && c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                throw std::runtime_error("Invalid body size");
            if (digit >= base
                || result > (std::numeric_limits<size_t>::max() - digit) / base)
                throw std::runtime_error("Invalid body size");
            result = result * base + digit;
        }
        return result;
    }

    void validateExtensions(const std::string& line, size_t pos)
    {
        while (pos < line.size())
        {
            if (line[pos++] != ';')
                throw std::runtime_error("Invalid chunk extension");
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            size_t start = pos;
            while (pos < line.size()
                && isTokenChar(static_cast<unsigned char>(line[pos])))
                ++pos;
            if (pos == start)
                throw std::runtime_error("Invalid chunk extension");
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            if (pos < line.size() && line[pos] == '=')
            {
                ++pos;
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                    ++pos;
                if (pos < line.size() && line[pos] == '"')
                {
                    ++pos;
                    bool closed = false;
                    while (pos < line.size())
                    {
                        unsigned char c = static_cast<unsigned char>(line[pos++]);
                        if (c == '"')
                        {
                            closed = true;
                            break;
                        }
                        if (c == '\\')
                        {
                            if (pos == line.size())
                                throw std::runtime_error("Invalid chunk extension");
                            c = static_cast<unsigned char>(line[pos++]);
                        }
                        if ((c < 32 && c != '\t') || c == 127)
                            throw std::runtime_error("Invalid chunk extension");
                    }
                    if (!closed)
                        throw std::runtime_error("Invalid chunk extension");
                }
                else
                {
                    start = pos;
                    while (pos < line.size()
                        && isTokenChar(static_cast<unsigned char>(line[pos])))
                        ++pos;
                    if (pos == start)
                        throw std::runtime_error("Invalid chunk extension");
                }
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                    ++pos;
            }
        }
    }
    typedef std::map<std::string, std::string> HeaderMap;

    HeaderMap parseHeaders(const std::string& buffer)
    {
        HeaderMap headers;
        size_t headerEnd = buffer.find("\r\n\r\n");
        size_t pos = buffer.find("\r\n");
        if (headerEnd == std::string::npos || pos == std::string::npos)
            throw std::runtime_error("Incomplete headers");
        pos += 2;
        while (pos < headerEnd)
        {
            size_t end = buffer.find("\r\n", pos);
            if (end == std::string::npos || end > headerEnd)
                throw std::runtime_error("Invalid header");
            std::string key;
            std::string value;
            parseHeader(buffer.substr(pos, end - pos), key, value);
            HeaderMap::iterator existing = headers.find(key);
            if (existing != headers.end())
            {
                if (key == "host" || key == "content-length"
                    || key == "transfer-encoding")
                    throw std::runtime_error("Duplicate framing or Host header");
                existing->second += ", " + value;
            }
            else
                headers.insert(std::make_pair(key, value));
            pos = end + 2;
        }
        if (headers.find("content-length") != headers.end()
            && headers.find("transfer-encoding") != headers.end())
            throw std::runtime_error("Content-Length conflicts with Transfer-Encoding");
        return headers;
    }

    int nextRequestBytes(size_t bytes)
    {
        if (bytes > static_cast<size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("Too many buffered request bytes");
        return static_cast<int>(bytes);
    }

    bool readChunks(const std::string& buffer, size_t pos,
        size_t limit, std::string* body, size_t& consumed)
    {
        size_t total = 0;
        while (true)
        {
            size_t end = buffer.find("\r\n", pos);
            if (end == std::string::npos)
                return false;
            std::string line = buffer.substr(pos, end - pos);
            size_t extension = line.find(';');
            size_t sizeEnd = extension == std::string::npos ? line.size() : extension;
            if (extension != std::string::npos)
                while (sizeEnd > 0 && (line[sizeEnd - 1] == ' ' || line[sizeEnd - 1] == '\t'))
                    --sizeEnd;
            size_t chunkSize = parseSize(line.substr(0, sizeEnd), 16);
            if (extension != std::string::npos)
                validateExtensions(line, extension);
            pos = end + 2;
            if (chunkSize == 0)
            {
                while (true)
                {
                    end = buffer.find("\r\n", pos);
                    if (end == std::string::npos)
                        return false;
                    if (end == pos)
                    {
                        consumed = end + 2;
                        return true;
                    }
                    std::string key;
                    std::string value;
                    parseHeader(buffer.substr(pos, end - pos), key, value);
                    if (key == "host" || key == "content-length"
                        || key == "transfer-encoding")
                        throw std::runtime_error("Invalid framing trailer");
                    pos = end + 2;
                }
            }
            if (chunkSize > limit - total)
                throw std::runtime_error("Body too long");
            total += chunkSize;
            // Subtraction avoids overflow when a chunk advertises a huge size.
            if (chunkSize > buffer.size() - pos)
                return false;
            size_t next = pos + chunkSize;
            if (buffer.size() - next < 2)
                return false;
            if (buffer.compare(next, 2, "\r\n") != 0)
                throw std::runtime_error("Invalid chunk ending");
            if (body)
                body->append(buffer, pos, chunkSize);
            pos = next + 2;
        }
    }
}

Request::Request(const Client& client)
    : Client(client), _TransferMethod(""), _nextRequestBytes(0),
      _contentLen(0), _finisedHeader(false), _hasContent(false),
      _hasTransferEncoding(false)
{
}

Request::~Request() {}

bool Request::getHasContentStatus() { return _hasContent; }
bool Request::getTransferEncodingStatus() { return _hasTransferEncoding; }
size_t Request::getContentLength() const { return _contentLen; }

bool Request::checkHeader(const std::string buffer)
{
    _finisedHeader = buffer.find("\r\n\r\n") != std::string::npos;
    _hasContent = false;
    _hasTransferEncoding = false;
    _headers.clear();
    if (!_finisedHeader)
        return false;
    _headers = parseHeaders(buffer);
    _hasContent = _headers.find("content-length") != _headers.end();
    _hasTransferEncoding = _headers.find("transfer-encoding") != _headers.end();
    return true;
}

void Request::HeadersToMap(const std::string request)
{
    if (!checkHeader(request))
        throw std::runtime_error("Incomplete headers");
    std::map<std::string, std::string>::const_iterator host = _headers.find("host");
    if (host == _headers.end())
        throw std::runtime_error("No host");
    HostParsing(host->second);
}

void Request::HostParsing(std::string host)
{
    if (getFd() < 0)
        throw std::runtime_error("Request requires a connected client");
    host = lowerCase(trim(host));
    if (host.empty() || host.find_first_of(" \t\r\n/@\\") != std::string::npos)
        throw std::runtime_error("Invalid host");
    size_t colon;
    if (host[0] == '[')
    {
        size_t closing = host.find(']');
        if (closing == std::string::npos || closing == 1)
            throw std::runtime_error("Invalid host");
        colon = closing + 1;
        if (colon == host.size())
            colon = std::string::npos;
        else if (host[colon] != ':')
            throw std::runtime_error("Invalid host");
    }
    else
        colon = host.find(':');
    if (colon != std::string::npos)
    {
        size_t port = parseSize(host.substr(colon + 1), 10);
        if (port == 0 || port > 65535)
            throw std::runtime_error("Invalid host port");
        host = host.substr(0, colon);
    }
    if (host.empty())
        throw std::runtime_error("Invalid host");
    if (host[0] == '[')
    {
        struct in6_addr address;
        if (inet_pton(AF_INET6, host.substr(1, host.size() - 2).c_str(), &address) != 1)
            throw std::runtime_error("Invalid IPv6 host");
        host = host.substr(1, host.size() - 2);
    }
    else
    {
        for (size_t i = 0; i < host.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(host[i]);
            if (!std::isalnum(c) && c != '.' && c != '-' && c != '_')
                throw std::runtime_error("Invalid host");
        }
    }

    // Keep a reference: setConfig stores a pointer to the selected configuration.
    host = canonicalHost(host);
    if (host.empty())
        throw std::runtime_error("Invalid host");
    const Listener& listener = getListener();
    if (listener.configs.empty())
        throw std::runtime_error("Listener has no server configuration");
    for (std::vector<ServerConfig>::const_iterator it = listener.configs.begin();
         it != listener.configs.end(); ++it)
    {
        std::vector<std::string> names = it->getServerName();
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (host == canonicalHost(names[i]))
            {
                setConfig(*it);
                return;
            }
        }
    }
    setConfig(listener.configs.front());
}

void Request::readingBody(const std::string buffer)
{
    // A persistent parser must not expose the body of the preceding request.
    _body.clear();
    _nextRequestBytes = 0;
    _contentLen = 0;
    _TransferMethod.clear();
    HeadersToMap(buffer);
    if (_hasContent)
    {
        _contentLen = parseSize(_headers.find("content-length")->second, 10);
        if (_contentLen > getConfig().getBodySize())
            throw std::runtime_error("Body too long");
    }
    else if (_hasTransferEncoding)
    {
        _TransferMethod = lowerCase(_headers.find("transfer-encoding")->second);
        if (_TransferMethod != "chunked")
            throw std::runtime_error("Unsupported transfer encoding");
    }
}

bool Request::checkingBody_framing(const std::string buffer)
{
    _nextRequestBytes = 0;
    if (!checkHeader(buffer))
        return false;
    readingBody(buffer);
    if (!_hasContent)
        throw std::runtime_error("No Content-Length");
    size_t bodyStart = buffer.find("\r\n\r\n") + 4;
    size_t bodyBytes = buffer.size() - bodyStart;
    if (bodyBytes < _contentLen)
        return false;
    _nextRequestBytes = nextRequestBytes(bodyBytes - _contentLen);
    return true;
}

bool Request::checkingBody_chuncked(const std::string buffer)
{
    _nextRequestBytes = 0;
    if (!checkHeader(buffer))
        return false;
    readingBody(buffer);
    if (!_hasTransferEncoding)
        throw std::runtime_error("No Transfer-Encoding");
    size_t consumed = 0;
    if (!readChunks(buffer, buffer.find("\r\n\r\n") + 4,
        getConfig().getBodySize(), NULL, consumed))
        return false;
    _nextRequestBytes = nextRequestBytes(buffer.size() - consumed);
    return true;
}

void Request::RequestLineParsing(const std::string request)
{
    _requestLine.clear();
    size_t end = request.find("\r\n");
    if (end == std::string::npos)
        throw std::runtime_error("Invalid request line");
    std::string line = request.substr(0, end);
    size_t methodEnd = line.find(' ');
    if (methodEnd == std::string::npos)
        throw std::runtime_error("Invalid request method");
    std::string method = line.substr(0, methodEnd);
    if (method != "GET" && method != "POST" && method != "DELETE")
        throw std::runtime_error("Invalid method");
    size_t targetStart = methodEnd + 1;
    size_t targetEnd = line.find(' ', targetStart);
    if (targetEnd == std::string::npos || targetEnd == targetStart)
        throw std::runtime_error("Invalid request target");
    std::string target = line.substr(targetStart, targetEnd - targetStart);
    if (target[0] != '/' || target.find('#') != std::string::npos)
        throw std::runtime_error("Invalid request target");
    for (size_t i = 0; i < target.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(target[i]);
        if (c <= 32 || c == 127)
            throw std::runtime_error("Invalid request target");
        if (c == '%')
        {
            if (target.size() - i < 3
                || !std::isxdigit(static_cast<unsigned char>(target[i + 1]))
                || !std::isxdigit(static_cast<unsigned char>(target[i + 2])))
                throw std::runtime_error("Invalid percent encoding");
            i += 2;
        }
    }
    if (line.substr(targetEnd + 1) != "HTTP/1.1")
        throw std::runtime_error("Invalid HTTP version");
    _requestLine["Method"] = method;
    _requestLine["Version"] = "HTTP/1.1";
    if (target.find('?') != std::string::npos)
        QueryParsing(target);
    else
        _requestLine["Path"] = target;
}

void Request::QueryParsing(const std::string requestTarget)
{
    size_t pos = requestTarget.find('?');
    if (pos == std::string::npos || pos == 0)
        throw std::runtime_error("Invalid query target");
    _requestLine["Path"] = requestTarget.substr(0, pos);
    _requestLine["Query"] = requestTarget.substr(pos + 1);
}

void Request::MethodParsing(std::string method)
{
    if (method != "GET" && method != "POST" && method != "DELETE")
        throw std::runtime_error("Invalid method");
    if (_headers.find("host") == _headers.end())
        throw std::runtime_error("No host");
    HostParsing(_headers.find("host")->second);
    if (_requestLine.find("Path") == _requestLine.end())
        throw std::runtime_error("Request line not parsed");
    const std::string& path = _requestLine.find("Path")->second;
    std::vector<Locations> locations = getConfig().getLocations();
    const Locations* selected = NULL;
    size_t longest = 0;
    for (size_t i = 0; i < locations.size(); ++i)
    {
        std::string prefix = locations[i].getPath();
        if (!prefix.empty() && path.compare(0, prefix.size(), prefix) == 0
            && (!selected || prefix.size() > longest))
        {
            selected = &locations[i];
            longest = prefix.size();
        }
    }
    if (!selected)
        throw std::runtime_error("Location not found");
    std::vector<std::string> methods = selected->getAllowedMethods();
    // An omitted allow_methods directive does not impose a method restriction.
    if (methods.empty())
        return;
    for (size_t i = 0; i < methods.size(); ++i)
        if (method == methods[i])
            return;
    throw std::runtime_error("Method not allowed");
}

void Request::parseChunkedBody(const std::string& buffer, size_t pos)
{
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos || pos != headerEnd + 4)
        throw std::runtime_error("Invalid body offset");
    readingBody(buffer);
    if (!_hasTransferEncoding)
        throw std::runtime_error("No Transfer-Encoding");
    std::string body;
    size_t consumed = 0;
    if (!readChunks(buffer, pos, getConfig().getBodySize(), &body, consumed))
        throw std::runtime_error("Incomplete chunked body");
    _body.swap(body);
    _contentLen = _body.size();
    _nextRequestBytes = nextRequestBytes(buffer.size() - consumed);
}

void Request::parseBody(const std::string& buffer)
{
    readingBody(buffer);
    size_t bodyStart = buffer.find("\r\n\r\n") + 4;
    if (_hasContent)
    {
        if (buffer.size() - bodyStart < _contentLen)
            throw std::runtime_error("Incomplete body");
        _body = buffer.substr(bodyStart, _contentLen);
        _nextRequestBytes = nextRequestBytes(buffer.size() - bodyStart - _contentLen);
    }
    else if (_hasTransferEncoding)
        parseChunkedBody(buffer, bodyStart);
    else
    {
        _body.clear();
        _nextRequestBytes = nextRequestBytes(buffer.size() - bodyStart);
    }
}

void Request::start_parsing(const std::string requestBuffer)
{
    _requestLine.clear();
    _body.clear();
    _contentLen = 0;
    _TransferMethod.clear();
    _nextRequestBytes = 0;
    if (!checkHeader(requestBuffer))
        throw std::runtime_error("Incomplete headers");
    RequestLineParsing(requestBuffer);
    HeadersToMap(requestBuffer);
    MethodParsing(_requestLine["Method"]);
    parseBody(requestBuffer);
    for (std::map<std::string, std::string>::const_iterator it = _requestLine.begin();
         it != _requestLine.end(); ++it)
        std::cout << it->first << ":" << it->second << std::endl;
    std::cout << std::endl;
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
        std::cout << it->first << ":" << it->second << std::endl;
    std::cout << "Parsing done" << std::endl;
}

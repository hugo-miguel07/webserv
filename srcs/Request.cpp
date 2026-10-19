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

#include "Request.hpp"
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>


namespace
{
const size_t MAX_HEADER_BYTES = 8192;
const size_t MAX_URI_BYTES = 2048;

std::string toLower(const std::string &s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = std::tolower(static_cast<unsigned char>(out[i]));
    return out;
}

std::string trim(const std::string &s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
        ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t'))
        --end;
    return s.substr(begin, end - begin);
}

bool isTokenChar(unsigned char c)
{
    if (c <= 32 || c >= 127)
        return false;
    switch (c)
    {
        case '(': case ')': case '<': case '>': case '@':
        case ',': case ';': case ':': case '\\': case '"':
        case '/': case '[': case ']': case '?': case '=':
        case '{': case '}':
            return false;
        default:
            return true;
    }
}

bool isToken(const std::string &s)
{
    if (s.empty())
        return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (!isTokenChar(static_cast<unsigned char>(s[i])))
            return false;
    }
    return true;
}

bool isDigits(const std::string &s)
{
    if (s.empty())
        return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    return true;
}

bool isKnownMethod(const std::string &method)
{
    return (method == "GET" || method == "HEAD" || method == "POST"
            || method == "PUT" || method == "DELETE" || method == "OPTIONS");
}

void splitLines(const std::string &s, std::vector<std::string> &out)
{
    size_t start = 0;
    while (start <= s.size())
    {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos)
        {
            out.push_back(s.substr(start));
            break;
        }
        std::string line = s.substr(start, nl - start);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        out.push_back(line);
        start = nl + 1;
    }
}

void splitTokens(const std::string &line, std::vector<std::string> &out)
{
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
}

bool splitHeader(const std::string &line, std::string &name, std::string &value)
{
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0)
        return false;
    name = line.substr(0, colon);
    if (!isToken(name))
        return false;
    value = trim(line.substr(colon + 1));
    return true;
}

bool findHeaderEnd(const std::string &buf, size_t &headerEnd, size_t &headerLen)
{
    size_t crlf = buf.find("\r\n\r\n");
    size_t lf = buf.find("\n\n");
    if (crlf == std::string::npos && lf == std::string::npos)
        return false;
    if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf))
    {
        headerEnd = crlf;
        headerLen = 4;
    }
    else
    {
        headerEnd = lf;
        headerLen = 2;
    }
    return true;
}

enum e_chunkWalk
{
    CHUNK_OK = 0,
    CHUNK_INCOMPLETE,
    CHUNK_BAD
};

int walkChunked(const std::string &s, size_t pos,
                std::string *bodyOut, size_t *endPos)
{
    size_t p = pos;
    std::string decoded;

    while (true)
    {
        size_t eol = s.find("\r\n", p);
        if (eol == std::string::npos)
            return CHUNK_INCOMPLETE;
        std::string sizeLine = s.substr(p, eol - p);

        size_t semi = sizeLine.find(';');
        std::string hex = trim(semi == std::string::npos
                               ? sizeLine : sizeLine.substr(0, semi));
        if (hex.empty() || hex.size() > 8)
            return CHUNK_BAD;
        for (size_t i = 0; i < hex.size(); ++i)
        {
            if (!std::isxdigit(static_cast<unsigned char>(hex[i])))
                return CHUNK_BAD;
        }
        size_t size = static_cast<size_t>(std::strtoul(hex.c_str(), NULL, 16));
        p = eol + 2;

        if (size == 0)
        {
            while (true)
            {
                size_t te = s.find("\r\n", p);
                if (te == std::string::npos)
                    return CHUNK_INCOMPLETE;
                if (te == p)
                {
                    if (endPos)
                        *endPos = p + 2;
                    if (bodyOut)
                        *bodyOut += decoded;
                    return CHUNK_OK;
                }
                p = te + 2;
            }
        }

        if (p + size + 2 > s.size())
            return CHUNK_INCOMPLETE;
        if (s[p + size] != '\r' || s[p + size + 1] != '\n')
            return CHUNK_BAD;
        decoded.append(s, p, size);
        p += size + 2;
    }
}

void getFramingHeaders(const std::string &head,
                       std::string &contentLength,
                       std::string &transferEncoding)
{
    std::vector<std::string> lines;
    splitLines(head, lines);
    for (size_t i = 1; i < lines.size(); ++i)
    {
        if (lines[i].empty())
            continue;
        std::string name, value;
        if (!splitHeader(lines[i], name, value))
            continue;
        std::string lower = toLower(name);
        if (lower == "content-length")
            contentLength = value;
        else if (lower == "transfer-encoding")
            transferEncoding = value;
    }
}

}


Request::Request() : _valid(false), _errorStatus(400) {}

Request::~Request() {}

void Request::clear()
{
    method.clear();
    target.clear();
    path.clear();
    query.clear();
    version.clear();
    headers.clear();
    body.clear();
    _valid = false;
    _errorStatus = 400;
}

bool Request::isValid() const { return (_valid); }

int Request::getErrorStatus() const { return (_errorStatus); }


bool Request::parse(const std::string &raw)
{
    clear();

    size_t headerEnd = 0;
    size_t headerLen = 0;
    if (!findHeaderEnd(raw, headerEnd, headerLen))
        return false;
    if (headerEnd > MAX_HEADER_BYTES)
    {
        _errorStatus = 431;
        return false;
    }

    std::vector<std::string> lines;
    splitLines(raw.substr(0, headerEnd), lines);
    if (lines.empty())
        return false;

    std::vector<std::string> tokens;
    splitTokens(lines[0], tokens);
    if (tokens.size() != 3)
        return false;

    method = tokens[0];
    target = tokens[1];
    version = tokens[2];

    if (!isToken(method) || !isKnownMethod(method))
    {
        _errorStatus = 501;
        return false;
    }

    if (version != "HTTP/1.0" && version != "HTTP/1.1")
    {
        if (version.size() >= 5 && toLower(version).compare(0, 5, "http/") == 0)
            _errorStatus = 505;
        return false;
    }

    if (target.size() > MAX_URI_BYTES)
    {
        _errorStatus = 414;
        return false;
    }

    std::string t = target;
    std::string lowTarget = toLower(target);
    size_t schemeLen = 0;
    if (lowTarget.compare(0, 7, "http://") == 0)
        schemeLen = 7;
    else if (lowTarget.compare(0, 8, "https://") == 0)
        schemeLen = 8;
    if (schemeLen != 0)
    {
        size_t slash = target.find('/', schemeLen);
        if (slash == std::string::npos)
        {
            if (target.size() > schemeLen)
                headers["host"] = target.substr(schemeLen);
            t = "/";
        }
        else
        {
            if (slash > schemeLen)
                headers["host"] = target.substr(schemeLen, slash - schemeLen);
            t = target.substr(slash);
        }
    }
    if (t.empty() || t[0] != '/')
        return false;

    size_t qmark = t.find('?');
    if (qmark == std::string::npos)
    {
        path = t;
        query = "";
    }
    else
    {
        path = t.substr(0, qmark);
        query = t.substr(qmark + 1);
    }
    path = urlDecode(path, false);
    if (path.empty())
        return false;

    for (size_t i = 1; i < lines.size(); ++i)
    {
        const std::string &line = lines[i];
        if (line.empty())
            continue;
        if (line[0] == ' ' || line[0] == '\t')
            return false;
        std::string name, value;
        if (!splitHeader(line, name, value))
            return false;
        std::string lower = toLower(name);
        std::map<std::string, std::string>::iterator it = headers.find(lower);
        if (it == headers.end())
            headers[lower] = value;
        else
            it->second += ", " + value;
    }

    std::string transferEncoding = toLower(getHeader("transfer-encoding"));
    std::string contentLength = trim(getHeader("content-length"));
    size_t bodyStart = headerEnd + headerLen;

    if (!transferEncoding.empty() && !contentLength.empty())
        return false;

    if (!transferEncoding.empty())
    {
        if (transferEncoding.find("chunked") == std::string::npos)
        {
            _errorStatus = 501;
            return false;
        }
        size_t endPos = 0;
        int walk = walkChunked(raw, bodyStart, &body, &endPos);
        if (walk != CHUNK_OK)
            return false;
    }
    else if (!contentLength.empty())
    {
        if (!isDigits(contentLength))
            return false;
        size_t length =
            static_cast<size_t>(std::strtoul(contentLength.c_str(), NULL, 10));
        body = raw.substr(bodyStart, length);
    }
    else if (method == "POST" || method == "PUT")
    {
        _errorStatus = 411;
        return false;
    }

    _valid = true;
    return true;
}


bool Request::isComplete(const std::string &buf)
{
    size_t headerEnd = 0;
    size_t headerLen = 0;
    if (!findHeaderEnd(buf, headerEnd, headerLen))
    {
        return (buf.size() > MAX_HEADER_BYTES);
    }

    std::string contentLength, transferEncoding;
    getFramingHeaders(buf.substr(0, headerEnd), contentLength, transferEncoding);

    size_t bodyStart = headerEnd + headerLen;

    std::string teLower = toLower(transferEncoding);
    if (teLower.find("chunked") != std::string::npos)
        return (walkChunked(buf, bodyStart, NULL, NULL) == CHUNK_OK);

    contentLength = trim(contentLength);
    if (!contentLength.empty() && isDigits(contentLength))
    {
        if (contentLength.size() > 12)
            return false;
        size_t length =
            static_cast<size_t>(std::strtoul(contentLength.c_str(), NULL, 10));
        return (buf.size() - bodyStart >= length);
    }

    return true;
}

size_t Request::peekContentLength(const std::string &buf)
{
    size_t headerEnd = 0;
    size_t headerLen = 0;
    if (!findHeaderEnd(buf, headerEnd, headerLen))
        return 0;

    std::string contentLength, transferEncoding;
    getFramingHeaders(buf.substr(0, headerEnd), contentLength, transferEncoding);
    (void)transferEncoding;

    contentLength = trim(contentLength);
    if (contentLength.empty() || !isDigits(contentLength))
        return 0;
    if (contentLength.size() > 12)
        return static_cast<size_t>(-1);
    return static_cast<size_t>(std::strtoul(contentLength.c_str(), NULL, 10));
}


std::string Request::getHeader(const std::string &name) const
{
    std::map<std::string, std::string>::const_iterator it =
        headers.find(toLower(name));
    if (it != headers.end())
        return (it->second);
    return ("");
}

bool Request::hasBody() const
{
    return (!body.empty());
}

size_t Request::getContentLength() const
{
    std::string value = trim(getHeader("content-length"));
    if (!isDigits(value) || value.size() > 12)
        return 0;
    return static_cast<size_t>(std::strtoul(value.c_str(), NULL, 10));
}

std::string Request::urlDecode(const std::string &s, bool plusAsSpace)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%')
        {
            if (i + 2 >= s.size()
                || !std::isxdigit(static_cast<unsigned char>(s[i + 1]))
                || !std::isxdigit(static_cast<unsigned char>(s[i + 2])))
                return "";
            char hex[3] = { s[i + 1], s[i + 2], '\0' };
            out += static_cast<char>(std::strtoul(hex, NULL, 16));
            i += 2;
        }
        else if (s[i] == '+' && plusAsSpace)
            out += ' ';
        else
            out += s[i];
    }
    return out;
}

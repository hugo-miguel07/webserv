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

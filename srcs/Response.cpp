#include "Response.hpp"
#include <sstream>
#include <ctime>

namespace
{
struct StatusPhrase
{
    int         code;
    const char *phrase;
};

const StatusPhrase k_phrases[] =
{
    {200, "OK"}, {201, "Created"}, {202, "Accepted"},
    {204, "No Content"}, {206, "Partial Content"},
    {301, "Moved Permanently"}, {302, "Found"}, {303, "See Other"},
    {304, "Not Modified"}, {307, "Temporary Redirect"},
    {308, "Permanent Redirect"},
    {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"},
    {404, "Not Found"}, {405, "Method Not Allowed"},
    {408, "Request Timeout"}, {409, "Conflict"}, {410, "Gone"},
    {411, "Length Required"}, {413, "Payload Too Large"},
    {414, "URI Too Long"}, {415, "Unsupported Media Type"},
    {418, "I'm a teapot"}, {431, "Request Header Fields Too Large"},
    {500, "Internal Server Error"}, {501, "Not Implemented"},
    {502, "Bad Gateway"}, {503, "Service Unavailable"},
    {504, "Gateway Timeout"}, {505, "HTTP Version Not Supported"}
};

const size_t k_phraseCount = sizeof(k_phrases) / sizeof(k_phrases[0]);

std::string toLower(const std::string &s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = std::tolower(static_cast<unsigned char>(out[i]));
    return out;
}

bool hasHeader(const std::map<std::string, std::string> &headers,
               const std::string &name)
{
    std::string lower = toLower(name);
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it)
    {
        if (toLower(it->first) == lower)
            return true;
    }
    return false;
}

std::string numberToString(size_t n)
{
    std::ostringstream out;
    out << n;
    return out.str();
}

std::string httpDate()
{
    char buf[64];
    std::time_t now = std::time(NULL);
    std::tm *tmValue = std::gmtime(&now);
    if (!tmValue || std::strftime(buf, sizeof(buf),
                                  "%a, %d %b %Y %H:%M:%S GMT", tmValue) == 0)
        return "";
    return std::string(buf);
}

std::string defaultErrorBody(int code)
{
    std::string phrase = Response::getReasonPhrase(code);
    std::ostringstream out;
    out << "<html>\r\n<head><title>" << code << " " << phrase
        << "</title></head>\r\n<body>\r\n<center><h1>" << code << " "
        << phrase << "</h1></center>\r\n<hr><center>webserv</center>\r\n"
        << "</body>\r\n</html>\r\n";
    return out.str();
}

}


Response::Response() : statusCode(200), reasonPhrase("OK") {}

Response::~Response() {}

void Response::clear()
{
    statusCode = 200;
    reasonPhrase = "OK";
    headers.clear();
    body.clear();
}


void Response::setStatus(int code, const std::string &reason)
{
    statusCode = code;
    reasonPhrase = reason.empty() ? getReasonPhrase(code) : reason;
}

void Response::setHeader(const std::string &name, const std::string &value)
{
    this->headers[name] = value;
}

void Response::setBody(const std::string &content)
{
    this->body = content;
}

bool Response::hasLocation() const
{
    return (hasHeader(headers, "Location"));
}


std::string Response::getReasonPhrase(int code)
{
    for (size_t i = 0; i < k_phraseCount; ++i)
    {
        if (k_phrases[i].code == code)
            return std::string(k_phrases[i].phrase);
    }
    return "Unknown";
}

void Response::setError(int code)
{
    setError(code, defaultErrorBody(code));
}

void Response::setError(int code, const std::string &customBody)
{
    setStatus(code, getReasonPhrase(code));
    body = customBody;
    headers["Content-Type"] = "text/html";
    headers["Content-Length"] = numberToString(body.size());
}


std::string Response::toString() const
{
    std::string reason = reasonPhrase.empty()
                         ? getReasonPhrase(statusCode) : reasonPhrase;

    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << " " << reason << "\r\n";

    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it)
    {
        out << it->first << ": " << it->second << "\r\n";
    }
    if (!hasHeader(headers, "Content-Length"))
        out << "Content-Length: " << body.size() << "\r\n";
    if (!hasHeader(headers, "Server"))
        out << "Server: webserv/1.0\r\n";
    if (!hasHeader(headers, "Date"))
    {
        std::string date = httpDate();
        if (!date.empty())
            out << "Date: " << date << "\r\n";
    }

    out << "\r\n" << body;
    return out.str();
}

#include "Response.hpp"

Response::Response() : statusCode(200), reasonPhrase("OK") {}

Response::~Response() {}

void Response::setStatus(int code, const std::string &reason)
{
    this->statusCode = code;
    this->reasonPhrase = reason;
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
    return (headers.find("Location") != headers.end());
}

#include "Request.hpp"

Request::Request() {}

Request::~Request() {}

std::string Request::getHeader(const std::string &name) const
{
    std::map<std::string, std::string>::const_iterator it = headers.find(name);
    if (it != headers.end())
        return (it->second);
    return ("");
}

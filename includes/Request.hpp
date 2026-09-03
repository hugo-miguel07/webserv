#pragma once
#include <string>
#include <map>
#include "test.hpp"

/*
    Parsed HTTP request.
    TODO: filled by the request parser (HTTP parsing not implemented yet).
*/
class Request
{
public:
    Request();
    ~Request();

    // Request line
    std::string method;         // "GET", "POST", ...
    std::string target;         // raw request target, e.g. "/index.html?key=v"
    std::string path;           // target without query string
    std::string query;          // query string without '?'
    std::string version;        // "HTTP/1.1"

    // Headers (header-name -> value, value keeps original casing)
    std::map<std::string, std::string> headers;

    // Body
    std::string body;

    // Convenience lookup; returns "" if the header is absent.
    std::string getHeader(const std::string &name) const;
};

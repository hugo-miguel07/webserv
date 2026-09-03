#pragma once
#include <string>
#include <map>
#include "test.hpp"

/*
    HTTP response being built by a handler, then streamed by the Server
    through Client's response buffer.
    TODO: serialization into raw bytes (status line + headers + body).
*/
class Response
{
public:
    Response();
    ~Response();

    int                                 statusCode;
    std::string                         reasonPhrase;   // e.g. "Not Found"
    std::map<std::string, std::string>  headers;
    std::string                         body;

    // Convenience setters.
    void        setStatus(int code, const std::string &reason);
    void        setHeader(const std::string &name, const std::string &value);
    void        setBody(const std::string &content);

    // True if a Location header was set (used for redirects).
    bool        hasLocation() const;
};

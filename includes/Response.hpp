#pragma once
#include <string>
#include <map>
#include "test.hpp"

class Response
{
public:
    Response();
    ~Response();

    int                                 statusCode;
    std::string                         reasonPhrase;
    std::map<std::string, std::string>  headers;
    std::string                         body;

    void        setStatus(int code, const std::string &reason);
    void        setHeader(const std::string &name, const std::string &value);
    void        setBody(const std::string &content);

    bool        hasLocation() const;

    void        clear();

    static std::string getReasonPhrase(int code);

    void        setError(int code);

    void        setError(int code, const std::string &customBody);

    std::string toString() const;
};

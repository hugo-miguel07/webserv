#pragma once
#include <string>
#include <map>
#include "test.hpp"

class Request
{
public:
    Request();
    ~Request();

    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;

    std::map<std::string, std::string> headers;

    std::string body;


    bool parse(const std::string &raw);

    bool isValid() const;

    int getErrorStatus() const;

    void clear();

    static bool isComplete(const std::string &buf);

    static size_t peekContentLength(const std::string &buf);


    std::string getHeader(const std::string &name) const;

    bool hasBody() const;

    size_t getContentLength() const;

    static std::string urlDecode(const std::string &s, bool plusAsSpace);

private:
    bool    _valid;
    int     _errorStatus;
};

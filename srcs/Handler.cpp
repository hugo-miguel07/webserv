#include "Handler.hpp"
#include <sys/stat.h>

Handler::Handler() {}

Handler::~Handler() {}


void Handler::buildError(Response &res, int statusCode) const
{
    res.clear();
    res.setError(statusCode);
}

bool Handler::resolvePath(const std::string &root,
                          const std::string &requestPath,
                          std::string &outPath) const
{

    if (requestPath.find('\0') != std::string::npos)
        return false;

    std::string base = root;
    while (base.size() > 1 && base[base.size() - 1] == '/')
        base.erase(base.size() - 1);
    if (base == "/")
        base.clear();

    std::vector<std::string> segments;
    size_t i = 0;
    while (i < requestPath.size())
    {
        while (i < requestPath.size() && requestPath[i] == '/')
            ++i;
        size_t start = i;
        while (i < requestPath.size() && requestPath[i] != '/')
            ++i;
        if (i == start)
            continue;
        std::string segment = requestPath.substr(start, i - start);
        if (segment == ".")
            continue;
        if (segment == "..")
        {
            if (segments.empty())
                return false;
            segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }

    outPath = base;
    for (size_t s = 0; s < segments.size(); ++s)
        outPath += "/" + segments[s];
    if (outPath.empty())
        outPath = "/";
    return true;
}

bool Handler::isMethodAllowed(const Request &req, const Locations &loc) const
{
    const std::vector<std::string> &methods = loc.getAllowedMethods();

    if (methods.empty())
        return true;

    for (size_t i = 0; i < methods.size(); ++i)
    {
        if (methods[i] == req.method)
            return true;
    }
    return false;
}


Handler::e_status Handler::notAllowed(Response &res, const Locations &loc) const
{
    const std::vector<std::string> &methods = loc.getAllowedMethods();
    std::string allow;
    for (size_t i = 0; i < methods.size(); ++i)
    {
        if (i > 0)
            allow += ", ";
        allow += methods[i];
    }
    buildError(res, 405);
    if (!allow.empty())
        res.setHeader("Allow", allow);
    return (DONE);
}

bool Handler::isFile(const std::string &path) const
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode) != 0;
}

bool Handler::isDirectory(const std::string &path) const
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISDIR(st.st_mode) != 0;
}

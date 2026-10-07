#pragma once
#include "Handler.hpp"

class StaticHandler : public Handler
{
public:
    StaticHandler();
    ~StaticHandler();

    e_status handle(const Request &req, Response &res,
                    const ServerConfig &srv, const Locations &loc);

private:
    e_status serveFile(const std::string &filePath,
                       const Request &req,
                       Response &res);

    bool    serveIndex(const std::string &dirPath,
                       const std::vector<std::string> &indexes,
                       const Request &req,
                       Response &res);

    e_status serveDirectoryListing(const Request &req,
                                   Response &res,
                                   const std::string &dirPath);

    const char *guessContentType(const std::string &filePath) const;
};

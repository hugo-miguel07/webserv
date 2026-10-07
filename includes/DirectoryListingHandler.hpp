#pragma once
#include "Handler.hpp"

class DirectoryListingHandler : public Handler
{
public:
    DirectoryListingHandler();
    ~DirectoryListingHandler();

    e_status handle(const Request &req, Response &res,
                    const ServerConfig &srv, const Locations &loc);

    e_status serve(Response &res,
                   const std::string &dirPath,
                   const std::string &urlPath);

private:
    std::vector<std::string>    listEntries(const std::string &dirPath) const;

    std::string                 buildHtml(const std::string &dirPath,
                                          const std::string &urlPath,
                                          const std::vector<std::string> &entries) const;

    static std::string          urlEncode(const std::string &raw);

    static std::string          htmlEscape(const std::string &raw);
};

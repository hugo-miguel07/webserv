#pragma once
#include "Handler.hpp"

/*
    Builds an HTML autoindex page: one line per entry in dirPath,
    links relative to the request path. Used both as fallback for "/" and
    for explicit directory requests when no index file matched.
*/
class DirectoryListingHandler : public Handler
{
public:
    DirectoryListingHandler();
    ~DirectoryListingHandler();

    e_status handle(const Request &req, Response &res);

private:
    // List directory entries sorted alphabetically.
    std::vector<std::string>    listEntries(const std::string &dirPath) const;

    // Build the HTML page for the given entries.
    std::string                 buildHtml(const std::string &dirPath,
                                          const std::string &urlPath,
                                          const std::vector<std::string> &entries) const;
};

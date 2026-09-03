#include "DirectoryListingHandler.hpp"

DirectoryListingHandler::DirectoryListingHandler() {}

DirectoryListingHandler::~DirectoryListingHandler() {}

Handler::e_status DirectoryListingHandler::handle(const Request &req, Response &res)
{
    (void)req;
    (void)res;
    return (DONE);
}

std::vector<std::string> DirectoryListingHandler::listEntries(
    const std::string &dirPath) const
{
    (void)dirPath;
    return (std::vector<std::string>());
}

std::string DirectoryListingHandler::buildHtml(
    const std::string &dirPath,
    const std::string &urlPath,
    const std::vector<std::string> &entries) const
{
    (void)dirPath;
    (void)urlPath;
    (void)entries;
    return ("");
}

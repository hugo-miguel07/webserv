#include "StaticHandler.hpp"

StaticHandler::StaticHandler() {}

StaticHandler::~StaticHandler() {}

Handler::e_status StaticHandler::handle(const Request &req, Response &res)
{
    (void)req;
    (void)res;
    return (DONE);
}

bool StaticHandler::isFile(const std::string &path) const
{
    (void)path;
    return (false);
}

bool StaticHandler::isDirectory(const std::string &path) const
{
    (void)path;
    return (false);
}

Handler::e_status StaticHandler::serveFile(const std::string &filePath,
                                  const Request &req,
                                  Response &res)
{
    (void)filePath;
    (void)req;
    (void)res;
    return (DONE);
}

Handler::e_status StaticHandler::serveIndex(const std::string &dirPath,
                                   const std::vector<std::string> &indexes,
                                   const Request &req,
                                   Response &res)
{
    (void)dirPath;
    (void)indexes;
    (void)req;
    (void)res;
    return (DONE);
}

Handler::e_status StaticHandler::serveDirectoryListing(const std::string &dirPath,
                                              const Request &req,
                                              Response &res)
{
    (void)dirPath;
    (void)req;
    (void)res;
    return (DONE);
}

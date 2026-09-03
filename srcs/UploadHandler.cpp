#include "UploadHandler.hpp"

UploadHandler::UploadHandler() {}

UploadHandler::~UploadHandler() {}

Handler::e_status UploadHandler::handle(const Request &req, Response &res)
{
    (void)req;
    (void)res;
    return (DONE);
}

bool UploadHandler::isBodyTooLarge(const Request &req,
                                   const ServerConfig &srv) const
{
    (void)req;
    (void)srv;
    return (false);
}

std::string UploadHandler::extractFilename(const Request &req) const
{
    (void)req;
    return ("");
}

bool UploadHandler::saveFile(const std::string &storeDir,
                             const std::string &filename,
                             const std::string &body) const
{
    (void)storeDir;
    (void)filename;
    (void)body;
    return (false);
}

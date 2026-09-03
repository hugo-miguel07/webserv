#include "DeleteHandler.hpp"

DeleteHandler::DeleteHandler() {}

DeleteHandler::~DeleteHandler() {}

Handler::e_status DeleteHandler::handle(const Request &req, Response &res)
{
    (void)req;
    (void)res;
    return (DONE);
}

bool DeleteHandler::removeFile(const std::string &path) const
{
    (void)path;
    return (false);
}

bool DeleteHandler::removeDirectory(const std::string &path) const
{
    (void)path;
    return (false);
}

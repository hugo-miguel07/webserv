#include "RedirectHandler.hpp"

RedirectHandler::RedirectHandler() {}

RedirectHandler::~RedirectHandler() {}

Handler::e_status RedirectHandler::handle(const Request &req, Response &res)
{
    (void)req;
    (void)res;
    return (DONE);
}

std::string RedirectHandler::buildTarget(const Request &req,
                                         const std::string &redirectConfig) const
{
    (void)req;
    (void)redirectConfig;
    return ("");
}

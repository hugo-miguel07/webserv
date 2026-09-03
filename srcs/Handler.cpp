#include "Handler.hpp"

Handler::Handler() {}

Handler::~Handler() {}

void Handler::buildError(Response &res, int statusCode) const
{
    (void)res;
    (void)statusCode;
}

bool Handler::resolvePath(const std::string &root,
                          const std::string &requestPath,
                          std::string &outPath) const
{
    (void)root;
    (void)requestPath;
    (void)outPath;
    return (false);
}

bool Handler::isMethodAllowed(const Request &req, const Locations &loc) const
{
    (void)req;
    (void)loc;
    return (false);
}

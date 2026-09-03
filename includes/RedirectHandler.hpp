#pragma once
#include "Handler.hpp"

/*
    Produces redirect responses: sets Location + status (301/302/...)
    from the location block's redirect configuration.
    Useful for host canonicalization or location-level redirects.
*/
class RedirectHandler : public Handler
{
public:
    RedirectHandler();
    ~RedirectHandler();

    e_status handle(const Request &req, Response &res);

private:
    // Build target URL from redirect config + request (scheme/host/path).
    std::string buildTarget(const Request &req,
                            const std::string &redirectConfig) const;
};

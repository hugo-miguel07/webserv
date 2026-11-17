#pragma once
#include "Handler.hpp"

class RedirectHandler : public Handler
{
public:
    RedirectHandler();
    ~RedirectHandler();

    e_status handle(const Request &req, Response &res,
                    const ServerConfig &srv, const Locations &loc);

private:
    std::string buildTarget(const Request &req,
                            const std::string &redirectConfig) const;
};

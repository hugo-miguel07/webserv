#pragma once
#include "Handler.hpp"

class DeleteHandler : public Handler
{
public:
    DeleteHandler();
    ~DeleteHandler();

    e_status handle(const Request &req, Response &res,
                    const ServerConfig &srv, const Locations &loc);

private:
    bool    removeFile(const std::string &path) const;

    bool    removeDirectory(const std::string &path) const;

    e_status deleteError(Response &res, int err) const;
};

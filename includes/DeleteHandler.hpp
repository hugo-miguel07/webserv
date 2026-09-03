#pragma once
#include "Handler.hpp"

/*
    Handles DELETE requests: removes the target file on disk after
    path validation. Directory deletion is optional / restricted.
*/
class DeleteHandler : public Handler
{
public:
    DeleteHandler();
    ~DeleteHandler();

    e_status handle(const Request &req, Response &res);

private:
    // Remove a regular file; false on failure.
    bool    removeFile(const std::string &path) const;

    // Remove an empty directory (optional feature).
    bool    removeDirectory(const std::string &path) const;
};

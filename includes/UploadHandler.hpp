#pragma once
#include "Handler.hpp"

/*
    Handles POST uploads: reads the request body (multipart/form-data or
    raw) and stores the file under the location's upload_store directory.
    Enforces client_max_body_size.
*/
class UploadHandler : public Handler
{
public:
    UploadHandler();
    ~UploadHandler();

    e_status handle(const Request &req, Response &res);

private:
    // Check Content-Length / body size against ServerConfig max body size.
    bool    isBodyTooLarge(const Request &req,
                           const ServerConfig &srv) const;

    // Extract the uploaded filename from the request (multipart or query).
    std::string extractFilename(const Request &req) const;

    // Write req.body to disk under storeDir. Returns false on I/O error.
    bool    saveFile(const std::string &storeDir,
                     const std::string &filename,
                     const std::string &body) const;
};

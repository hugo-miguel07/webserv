#pragma once
#include "Handler.hpp"

class UploadHandler : public Handler
{
public:
    UploadHandler();
    ~UploadHandler();

    e_status handle(const Request &req, Response &res,
                    const ServerConfig &srv, const Locations &loc);

private:
    struct FilePart
    {
        std::string filename;
        std::string content;
    };

    bool    isBodyTooLarge(const Request &req,
                           const ServerConfig &srv) const;

    std::string extractFilename(const Request &req) const;

    bool    saveFile(const std::string &storeDir,
                     const std::string &filename,
                     const std::string &body) const;

    std::string getBoundary(const Request &req) const;

    std::string extractDispositionFilename(const std::string &partHeaders) const;

    bool    parseMultipart(const std::string &body,
                           const std::string &boundary,
                           std::vector<FilePart> &out) const;

    std::string sanitizeFilename(const std::string &raw) const;
};

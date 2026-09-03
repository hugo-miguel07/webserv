#pragma once
#include "Handler.hpp"

/*
    Serves static files from disk:
    GET / HEAD on an existing file, or index resolution + directory
    listing fallback inside a location block.
*/
class StaticHandler : public Handler
{
public:
    StaticHandler();
    ~StaticHandler();

    e_status handle(const Request &req, Response &res);

private:
    // True if path points to a regular file.
    bool    isFile(const std::string &path) const;

    // True if path points to a directory.
    bool    isDirectory(const std::string &path) const;

    // Serve the file content into the response body.
    e_status serveFile(const std::string &filePath,
                       const Request &req,
                       Response &res);

    // Try each configured index file inside a directory.
    e_status serveIndex(const std::string &dirPath,
                        const std::vector<std::string> &indexes,
                        const Request &req,
                        Response &res);

    // Generate an HTML directory listing into the response body.
    e_status serveDirectoryListing(const std::string &dirPath,
                                   const Request &req,
                                   Response &res);
};

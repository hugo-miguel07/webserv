#include "StaticHandler.hpp"
#include "DirectoryListingHandler.hpp"
#include <fstream>
#include <sstream>
#include <cctype>


StaticHandler::StaticHandler() {}

StaticHandler::~StaticHandler() {}

Handler::e_status StaticHandler::handle(const Request &req, Response &res,
                                        const ServerConfig &srv,
                                        const Locations &loc)
{
    if (!isMethodAllowed(req, loc))
        return (notAllowed(res, loc));
    if (req.method != "GET" && req.method != "HEAD")
    {
        buildError(res, 405);
        res.setHeader("Allow", "GET, HEAD");
        return (DONE);
    }

    const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                    : loc.getRoot();

    std::string filePath;
    if (!resolvePath(root, req.path, filePath))
    {
        buildError(res, 403);
        return (DONE);
    }

    if (isDirectory(filePath))
    {
        if (!req.path.empty() && req.path[req.path.size() - 1] != '/')
        {
            std::string location = req.path + "/";
            if (!req.query.empty())
                location += "?" + req.query;
            res.clear();
            res.setStatus(301, "Moved Permanently");
            res.setHeader("Location", location);
            return (DONE);
        }

        bool served = false;
        if (!loc.getIndexes().empty())
            served = serveIndex(filePath, loc.getIndexes(), req, res);
        else if (!srv.getIndex().empty())
            served = serveIndex(filePath, srv.getIndex(), req, res);
        if (served)
            return (DONE);

        if (loc.getAutoindex())
            return (serveDirectoryListing(req, res, filePath));

        buildError(res, 403);
        return (DONE);
    }

    if (isFile(filePath))
        return (serveFile(filePath, req, res));

    buildError(res, 404);
    return (DONE);
}


Handler::e_status StaticHandler::serveFile(const std::string &filePath,
                                           const Request &req,
                                           Response &res)
{
    std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        buildError(res, isFile(filePath) ? 403 : 404);
        return (DONE);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        buildError(res, 500);
        return (DONE);
    }

    std::string content = buffer.str();

    res.clear();
    res.setStatus(200, "OK");
    res.setBody(content);
    res.setHeader("Content-Type", guessContentType(filePath));

    if (req.method == "HEAD")
    {
        std::ostringstream len;
        len << content.size();
        res.setHeader("Content-Length", len.str());
        res.setBody("");
    }
    return (DONE);
}

bool StaticHandler::serveIndex(const std::string &dirPath,
                               const std::vector<std::string> &indexes,
                               const Request &req,
                               Response &res)
{
    for (size_t i = 0; i < indexes.size(); ++i)
    {
        std::string candidate = dirPath;
        if (candidate.empty() || candidate[candidate.size() - 1] != '/')
            candidate += "/";
        candidate += indexes[i];

        if (isFile(candidate))
            return (serveFile(candidate, req, res) == DONE);
    }
    return (false);
}

Handler::e_status StaticHandler::serveDirectoryListing(const Request &req,
                                                       Response &res,
                                                       const std::string &dirPath)
{
    DirectoryListingHandler lister;
    return (lister.serve(res, dirPath, req.path));
}


const char *StaticHandler::guessContentType(const std::string &filePath) const
{
    static const struct
    {
        const char *ext;
        const char *type;
    } k_types[] =
    {
        {".html", "text/html"},  {".htm", "text/html"},
        {".css", "text/css"},    {".js", "application/javascript"},
        {".json", "application/json"},
        {".png", "image/png"},   {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
        {".svg", "image/svg+xml"}, {".ico", "image/x-icon"},
        {".txt", "text/plain"},  {".pdf", "application/pdf"},
    };
    static const size_t k_count = sizeof(k_types) / sizeof(k_types[0]);

    size_t dot = filePath.rfind('.');
    if (dot == std::string::npos)
        return ("application/octet-stream");
    std::string ext = filePath.substr(dot);

    for (size_t i = 0; i < ext.size(); ++i)
        ext[i] = std::tolower(static_cast<unsigned char>(ext[i]));

    for (size_t i = 0; i < k_count; ++i)
    {
        if (ext == k_types[i].ext)
            return (k_types[i].type);
    }
    return ("application/octet-stream");
}

#include "StaticHandler.hpp"
#include "DirectoryListingHandler.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <sys/stat.h>

namespace
{
const size_t CACHE_MAX_ENTRIES = 32;
const size_t CACHE_MAX_FILE_SIZE = 256u * 1024u;

struct CachedFile
{
    struct stat metadata;
    std::string content;
};

// Shared by the short-lived handlers; at most 8 MiB of file contents.
std::map<std::string, CachedFile> fileCache;

bool sameFile(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino
        && a.st_size == b.st_size && a.st_mode == b.st_mode
        && a.st_uid == b.st_uid && a.st_gid == b.st_gid
        && a.st_mtime == b.st_mtime && a.st_ctime == b.st_ctime
#if defined(__linux__)
        && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
        && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec
#endif
        ;
}
}


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

    // A cached regular file needs one metadata check, without reopening it.
    if (serveCachedFile(filePath, req, res))
        return (DONE);
    const std::vector<std::string> indexes = loc.getIndexes().empty()
        ? srv.getIndex() : loc.getIndexes();
    // With several indexes, preserve their priority by checking them in order.
    if (!req.path.empty() && req.path[req.path.size() - 1] == '/'
        && indexes.size() == 1)
    {
        const std::string candidate = filePath
            + (filePath[filePath.size() - 1] == '/' ? "" : "/") + indexes[0];
        if (serveCachedFile(candidate, req, res))
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

        if (loc.getAutoIndex())
            return (serveDirectoryListing(req, res, filePath));

        buildError(res, 404);
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
    if (serveCachedFile(filePath, req, res))
        return (DONE);

    struct stat before;
    const bool cacheable = stat(filePath.c_str(), &before) == 0
        && S_ISREG(before.st_mode) && before.st_size >= 0
        && static_cast<size_t>(before.st_size) <= CACHE_MAX_FILE_SIZE;
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

    struct stat after;
    if (cacheable && stat(filePath.c_str(), &after) == 0
        && sameFile(before, after)
        && content.size() == static_cast<size_t>(after.st_size))
    {
        if (fileCache.size() >= CACHE_MAX_ENTRIES)
            fileCache.erase(fileCache.begin());
        CachedFile &entry = fileCache[filePath];
        entry.metadata = after;
        entry.content = content;
    }

    fileResponse(filePath, content, req, res);
    return (DONE);
}

bool StaticHandler::serveCachedFile(const std::string &filePath,
                                    const Request &req, Response &res)
{
    std::map<std::string, CachedFile>::iterator it = fileCache.find(filePath);
    if (it == fileCache.end())
        return false;
    struct stat current;
    if (stat(filePath.c_str(), &current) != 0
        || !S_ISREG(current.st_mode) || !sameFile(it->second.metadata, current))
    {
        fileCache.erase(it);
        return false;
    }
    fileResponse(filePath, it->second.content, req, res);
    return true;
}

void StaticHandler::fileResponse(const std::string &filePath,
                                 const std::string &content,
                                 const Request &req, Response &res) const
{

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

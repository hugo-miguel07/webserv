#include "UploadHandler.hpp"
#include <fstream>
#include <cctype>


UploadHandler::UploadHandler() {}

UploadHandler::~UploadHandler() {}

Handler::e_status UploadHandler::handle(const Request &req, Response &res,
                                        const ServerConfig &srv,
                                        const Locations &loc)
{
    if (!isMethodAllowed(req, loc))
        return (notAllowed(res, loc));
    if (req.method != "POST")
    {
        buildError(res, 405);
        res.setHeader("Allow", "POST");
        return (DONE);
    }

    if (isBodyTooLarge(req, srv))
    {
        buildError(res, 413);
        return (DONE);
    }

    std::string store = loc.getUploadStore();
    if (store.empty())
    {
        buildError(res, 500);
        return (DONE);
    }
    while (store.size() > 1 && store[store.size() - 1] == '/')
        store.erase(store.size() - 1);

    const std::string boundary = getBoundary(req);
    if (!boundary.empty())
    {
        std::vector<FilePart> parts;
        if (!parseMultipart(req.body, boundary, parts) || parts.empty())
        {
            buildError(res, 400);
            return (DONE);
        }

        std::string savedPaths;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            std::string name = sanitizeFilename(parts[i].filename);
            if (name.empty())
            {
                buildError(res, 400);
                return (DONE);
            }
            if (!saveFile(store, name, parts[i].content))
            {
                buildError(res, 500);
                return (DONE);
            }
            if (i > 0)
                savedPaths += ", ";
            savedPaths += store + "/" + name;
        }

        res.clear();
        res.setStatus(201, "Created");
        res.setHeader("Location", savedPaths);
        res.setHeader("Content-Type", "text/plain");
        res.setBody("Created: " + savedPaths + "\n");
        return (DONE);
    }

    std::string name = sanitizeFilename(extractFilename(req));
    if (name.empty())
    {
        buildError(res, 400);
        return (DONE);
    }
    if (!saveFile(store, name, req.body))
    {
        buildError(res, 500);
        return (DONE);
    }

    res.clear();
    res.setStatus(201, "Created");
    res.setHeader("Location", store + "/" + name);
    res.setHeader("Content-Type", "text/plain");
    res.setBody("Created: " + store + "/" + name + "\n");
    return (DONE);
}


bool UploadHandler::isBodyTooLarge(const Request &req,
                                   const ServerConfig &srv) const
{
    if (srv.getBodySize() <= 0)
        return true;

    return (req.body.size() > static_cast<size_t>(srv.getBodySize()) ||
            req.getContentLength() > static_cast<size_t>(srv.getBodySize()));
}

std::string UploadHandler::extractFilename(const Request &req) const
{
    const std::string &path = req.path;
    size_t slash = path.rfind('/');
    if (slash == std::string::npos)
        return (path);
    return (path.substr(slash + 1));
}

bool UploadHandler::saveFile(const std::string &storeDir,
                             const std::string &filename,
                             const std::string &body) const
{
    std::string dir = storeDir;
    while (dir.size() > 1 && dir[dir.size() - 1] == '/')
        dir.erase(dir.size() - 1);
    std::string full = dir + "/" + filename;

    std::ofstream file(full.c_str(),
                       std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (file.bad())
        return false;
    return true;
}


std::string UploadHandler::getBoundary(const Request &req) const
{
    std::string ct = req.getHeader("content-type");
    size_t pos = ct.find("boundary");
    if (pos == std::string::npos)
        return ("");

    pos = ct.find('=', pos);
    if (pos == std::string::npos)
        return ("");
    ++pos;
    if (pos < ct.size() && ct[pos] == '"')
    {
        size_t end = ct.find('"', pos + 1);
        if (end == std::string::npos)
            return ("");
        return (ct.substr(pos + 1, end - pos - 1));
    }
    size_t end = ct.find(';', pos);
    if (end == std::string::npos)
        end = ct.size();
    std::string token = ct.substr(pos, end - pos);
    size_t b = token.find_first_not_of(" \t");
    size_t e = token.find_last_not_of(" \t");
    if (b == std::string::npos)
        return ("");
    return (token.substr(b, e - b + 1));
}

std::string
UploadHandler::extractDispositionFilename(const std::string &partHeaders) const
{
    std::string lower;
    lower.reserve(partHeaders.size());
    for (size_t i = 0; i < partHeaders.size(); ++i)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(partHeaders[i])));

    size_t disp = lower.find("content-disposition");
    if (disp == std::string::npos)
        return ("");

    size_t fn = lower.find("filename=", disp);
    if (fn == std::string::npos)
        return ("");
    fn += 9;

    if (fn < partHeaders.size() && partHeaders[fn] == '"')
    {
        size_t end = partHeaders.find('"', fn + 1);
        if (end == std::string::npos)
            return ("");
        return (partHeaders.substr(fn + 1, end - fn - 1));
    }
    size_t end = partHeaders.find_first_of(";\r\n", fn);
    if (end == std::string::npos)
        end = partHeaders.size();
    return (partHeaders.substr(fn, end - fn));
}

bool UploadHandler::parseMultipart(const std::string &body,
                                   const std::string &boundary,
                                   std::vector<FilePart> &out) const
{
    const std::string dash = "--";
    const std::string delim = dash + boundary;
    const std::string crlf = "\r\n";

    size_t pos = body.find(delim);
    if (pos == std::string::npos)
        return false;

    while (pos != std::string::npos)
    {
        pos += delim.size();

        if (body.compare(pos, 2, dash) == 0)
            break;

        size_t lineEnd = body.find(crlf, pos);
        if (lineEnd == std::string::npos)
            return false;
        pos = lineEnd + crlf.size();

        size_t headEnd = body.find(crlf + crlf, pos);
        if (headEnd == std::string::npos)
            return false;
        std::string partHeaders = body.substr(pos, headEnd - pos);

        size_t contentStart = headEnd + crlf.size() + crlf.size();
        size_t next = body.find(crlf + delim, contentStart);
        if (next == std::string::npos)
            return false;

        std::string filename = extractDispositionFilename(partHeaders);
        if (!filename.empty())
        {
            FilePart part;
            part.filename = filename;
            part.content = body.substr(contentStart, next - contentStart);
            out.push_back(part);
        }
        pos = next + crlf.size();
    }
    return true;
}

std::string UploadHandler::sanitizeFilename(const std::string &raw) const
{
    if (raw.find('\0') != std::string::npos)
        return ("");

    size_t seg = 0;
    while (seg < raw.size())
    {
        size_t next = raw.find_first_of("/\\", seg);
        std::string token = (next == std::string::npos)
                                ? raw.substr(seg)
                                : raw.substr(seg, next - seg);
        if (token == "." || token == "..")
            return ("");
        if (next == std::string::npos)
            break;
        seg = next + 1;
    }

    size_t start = raw.find_last_of("/\\");
    std::string name = (start == std::string::npos) ? raw
                                                    : raw.substr(start + 1);
    if (name.empty() || name == "." || name == "..")
        return ("");

    std::string clean;
    for (size_t i = 0; i < name.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c >= 0x20 && c != 0x7F)
            clean += static_cast<char>(c);
    }
    if (clean.size() > 200)
        return ("");
    return (clean);
}

#include "DirectoryListingHandler.hpp"
#include <sstream>
#include <cctype>
#include <dirent.h>


DirectoryListingHandler::DirectoryListingHandler() {}

DirectoryListingHandler::~DirectoryListingHandler() {}

Handler::e_status DirectoryListingHandler::handle(const Request &req,
                                                  Response &res,
                                                  const ServerConfig &srv,
                                                  const Locations &loc)
{
    (void)req;

    const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                    : loc.getRoot();
    std::string dirPath;
    if (!resolvePath(root, req.path, dirPath) || !isDirectory(dirPath))
    {
        buildError(res, 404);
        return (DONE);
    }
    return (serve(res, dirPath, req.path));
}

Handler::e_status DirectoryListingHandler::serve(Response &res,
                                                 const std::string &dirPath,
                                                 const std::string &urlPath)
{
    std::vector<std::string> entries = listEntries(dirPath);
    res.clear();
    res.setStatus(200, "OK");
    res.setHeader("Content-Type", "text/html");
    res.setBody(buildHtml(dirPath, urlPath, entries));
    return (DONE);
}


std::vector<std::string>
DirectoryListingHandler::listEntries(const std::string &dirPath) const
{
    std::vector<std::string> entries;

    DIR *dir = opendir(dirPath.c_str());
    if (dir == NULL)
        return (entries);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name(entry->d_name);
        if (name == "." || name == "..")
            continue;
        entries.push_back(name);
    }
    closedir(dir);

    for (size_t i = 1; i < entries.size(); ++i)
    {
        std::string key = entries[i];
        size_t j = i;
        while (j > 0 && entries[j - 1] > key)
        {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = key;
    }
    return (entries);
}

std::string DirectoryListingHandler::buildHtml(const std::string &dirPath,
                                               const std::string &urlPath,
                                               const std::vector<std::string> &entries) const
{
    (void)dirPath;

    std::ostringstream out;
    out << "<html>\r\n<head><title>Index of " << htmlEscape(urlPath)
        << "</title></head>\r\n"
        << "<body>\r\n<h1>Index of " << htmlEscape(urlPath) << "</h1>\r\n<ul>\r\n";

    std::string encodedPath;
    size_t segStart = 0;
    for (;;)
    {
        size_t slash = urlPath.find('/', segStart);
        if (slash == std::string::npos)
        {
            encodedPath += urlEncode(urlPath.substr(segStart));
            break;
        }
        encodedPath += urlEncode(urlPath.substr(segStart, slash - segStart));
        encodedPath += '/';
        segStart = slash + 1;
    }
    if (encodedPath.empty() || encodedPath[encodedPath.size() - 1] != '/')
        encodedPath += '/';

    for (size_t i = 0; i < entries.size(); ++i)
    {
        std::string href = encodedPath + urlEncode(entries[i]);
        out << "<li><a href=\"" << href << "\">" << htmlEscape(entries[i])
            << "</a></li>\r\n";
    }

    out << "</ul>\r\n<hr><center>webserv</center>\r\n</body>\r\n</html>\r\n";
    return (out.str());
}

std::string DirectoryListingHandler::urlEncode(const std::string &raw)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return (out);
}

std::string DirectoryListingHandler::htmlEscape(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        char c = raw[i];
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return (out);
}

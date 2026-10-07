#include "DeleteHandler.hpp"
#include <cstdio>
#include <cstring>
#include <errno.h>


DeleteHandler::DeleteHandler() {}

DeleteHandler::~DeleteHandler() {}

Handler::e_status DeleteHandler::handle(const Request &req, Response &res,
                                        const ServerConfig &srv,
                                        const Locations &loc)
{
    (void)srv;

    const std::vector<std::string> &methods = loc.getAllowedMethods();
    bool policyAllows = false;
    for (size_t i = 0; i < methods.size(); ++i)
    {
        if (methods[i] == "DELETE")
        {
            policyAllows = true;
            break;
        }
    }
    if (!policyAllows)
    {
        if (methods.empty())
        {
            buildError(res, 405);
            res.setHeader("Allow", "DELETE");
        }
        else
            return (notAllowed(res, loc));
        return (DONE);
    }
    if (req.method != "DELETE")
    {
        return (notAllowed(res, loc));
    }

    const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                    : loc.getRoot();
    std::string filePath;
    if (!resolvePath(root, req.path, filePath))
    {
        buildError(res, 403);
        return (DONE);
    }

    if (isFile(filePath))
    {
        errno = 0;
        if (!removeFile(filePath))
            return (deleteError(res, errno));
        res.clear();
        res.setStatus(204, "No Content");
        return (DONE);
    }
    if (isDirectory(filePath))
    {
        errno = 0;
        if (!removeDirectory(filePath))
            return (deleteError(res, errno));
        res.clear();
        res.setStatus(204, "No Content");
        return (DONE);
    }

    buildError(res, 404);
    return (DONE);
}


bool DeleteHandler::removeFile(const std::string &path) const
{
    return (std::remove(path.c_str()) == 0);
}

bool DeleteHandler::removeDirectory(const std::string &path) const
{
    return (rmdir(path.c_str()) == 0);
}

Handler::e_status DeleteHandler::deleteError(Response &res, int err) const
{
    int code;
    switch (err)
    {
        case EACCES:
        case EPERM:
        case EROFS:
            code = 403;
            break;
        case ENOENT:
            code = 404;
            break;
        case ENOTEMPTY:
        case EEXIST:
            code = 409;
            break;
        default:
            code = 500;
            break;
    }
    buildError(res, code);
    return (DONE);
}

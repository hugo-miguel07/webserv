#include "CGIHandler.hpp"

CGIHandler::CGIHandler() : _cgiPid(-1) {}

CGIHandler::~CGIHandler() {}

Handler::e_status CGIHandler::handle(const Request &req, Response &res)
{
    (void)req;
    (void)res;
    return (DONE);
}

bool CGIHandler::isScript(const std::string &filePath,
                          const std::vector<std::string> &cgiExtensions) const
{
    (void)filePath;
    (void)cgiExtensions;
    return (false);
}

char **CGIHandler::buildEnv(const Request &req) const
{
    (void)req;
    return (NULL);
}

void CGIHandler::freeEnv(char **env) const
{
    (void)env;
}

bool CGIHandler::launch(const std::string &scriptPath, const Request &req)
{
    (void)scriptPath;
    (void)req;
    return (false);
}

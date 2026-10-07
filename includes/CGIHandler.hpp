#pragma once
#include "Handler.hpp"

class CGIHandler : public Handler
{
public:
    CGIHandler();
    ~CGIHandler();

    e_status handle(const Request &req, Response &res,
                    const ServerConfig &srv, const Locations &loc);

private:
    bool    isScript(const std::string &filePath,
                     const std::vector<std::string> &cgiExtensions) const;

    char    **buildEnv(const std::string &interpreter,
                       const std::string &scriptPath,
                       const Request &req);

    void    freeEnv(char **env) const;

    bool    launch(const std::string &scriptPath,
                   const std::string &interpreter, const Request &req);

    pid_t   _cgiPid;

    int     _stdinPipe[2];
    int     _stdoutPipe[2];

    std::vector<std::string>    _envStore;

public:
    pid_t   getCgiPid() const;
    int     getStdinFd() const;
    int     getStdoutFd() const;
};

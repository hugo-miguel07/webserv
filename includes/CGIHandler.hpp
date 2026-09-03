#pragma once
#include "Handler.hpp"

/*
    Executes CGI scripts (e.g. .py) via fork/execve over pipes:
    - pass request metadata through environment variables
    - pipe the request body to the child's stdin
    - read the child's stdout (CGI headers + body) into the response
    Non-blocking friendly: returns NEED_CGI so the Server can keep
    polling while the child runs.
*/
class CGIHandler : public Handler
{
public:
    CGIHandler();
    ~CGIHandler();

    e_status handle(const Request &req, Response &res);

private:
    // True if the resolved file has an extension configured for CGI
    // (e.g. ".py" from cgi_extension directive).
    bool    isScript(const std::string &filePath,
                     const std::vector<std::string> &cgiExtensions) const;

    // Build the envp array (REQUEST_METHOD, QUERY_STRING, CONTENT_LENGTH...).
    char    **buildEnv(const Request &req) const;

    // Free the envp array built by buildEnv.
    void    freeEnv(char **env) const;

    // fork + dup2 + execve the interpreter on the script.
    bool    launch(const std::string &scriptPath, const Request &req);

    // Child pid, -1 when no CGI process is running.
    pid_t   _cgiPid;
};

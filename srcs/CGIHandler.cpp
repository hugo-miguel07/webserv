#include "CGIHandler.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>


namespace
{

const int      CGI_TIMEOUT_MS = 5000;
const size_t   CGI_MAX_OUTPUT = 10u * 1024u * 1024u;

time_t nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (static_cast<time_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000);
}

void closeEnd(int &fd)
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

void closePipePair(int *p)
{
    closeEnd(p[0]);
    closeEnd(p[1]);
}

std::string toLowerCopy(const std::string &s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(out[i])));
    return (out);
}

std::string trimCopy(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return ("");
    size_t e = s.find_last_not_of(" \t");
    return (s.substr(b, e - b + 1));
}

bool writeAllNonblocking(int fd, const std::string &data, time_t deadline)
{
    size_t off = 0;
    while (off < data.size())
    {
        time_t left = deadline - nowMs();
        if (left <= 0)
            return (false);
        struct pollfd p;
        p.fd = fd;
        p.events = POLLOUT;
        p.revents = 0;
        int pr = poll(&p, 1, static_cast<int>(left));
        if (pr < 0 && errno == EINTR)
            continue;
        if (pr <= 0)
            return (false);
        ssize_t n = write(fd, data.data() + off, data.size() - off);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return (false);
        }
        off += static_cast<size_t>(n);
    }
    return (true);
}

std::string readAllWithDeadline(int fd, time_t deadline,
                                bool &timedOut, bool &tooBig)
{
    std::string out;
    char buf[4096];
    for (;;)
    {
        if (out.size() > CGI_MAX_OUTPUT)
        {
            tooBig = true;
            break;
        }
        int left = static_cast<int>(deadline - nowMs());
        if (left <= 0)
        {
            timedOut = true;
            break;
        }
        struct pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        int pr = poll(&p, 1, left);
        if (pr < 0 && errno == EINTR)
            continue;
        if (pr == 0)
        {
            timedOut = true;
            break;
        }
        if (pr < 0)
            break;
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        out.append(buf, static_cast<size_t>(n));
    }
    return (out);
}

bool parseCgiOutput(const std::string &raw, Response &res)
{
    size_t headEnd = raw.find("\r\n\r\n");
    size_t sepLen = 4;
    if (headEnd == std::string::npos)
    {
        headEnd = raw.find("\n\n");
        sepLen = 2;
    }
    if (headEnd == std::string::npos)
        return (false);

    std::string head = raw.substr(0, headEnd);
    std::string body = raw.substr(headEnd + sepLen);

    bool hasContentType = false;
    size_t pos = 0;
    while (pos < head.size())
    {
        size_t eol = head.find('\n', pos);
        std::string line = (eol == std::string::npos)
                               ? head.substr(pos)
                               : head.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? head.size() : eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return (false);
        std::string name = trimCopy(line.substr(0, colon));
        std::string value = trimCopy(line.substr(colon + 1));
        std::string lowerName = toLowerCopy(name);

        if (lowerName == "status")
        {
            int code = std::atoi(value.c_str());
            if (code < 100 || code > 599)
                return (false);
            size_t d = value.find_first_not_of("0123456789");
            std::string reason = (d == std::string::npos)
                                     ? std::string("")
                                     : trimCopy(value.substr(d));
            res.setStatus(code, reason);
        }
        else if (lowerName == "content-type")
        {
            res.setHeader("Content-Type", value);
            hasContentType = true;
        }
        else if (lowerName == "location")
        {
            res.setHeader("Location", value);
        }
        else
        {
            res.setHeader(name, value);
        }
    }

    if (!hasContentType)
        return (false);
    res.setBody(body);
    return (true);
}

}


CGIHandler::CGIHandler()
    : _cgiPid(-1),
      _stdinPipe(),
      _stdoutPipe(),
      _envStore()
{
    _stdinPipe[0] = -1;
    _stdinPipe[1] = -1;
    _stdoutPipe[0] = -1;
    _stdoutPipe[1] = -1;
}

CGIHandler::~CGIHandler()
{
    if (_cgiPid > 0)
    {
        kill(_cgiPid, SIGKILL);
        waitpid(_cgiPid, NULL, 0);
        _cgiPid = -1;
    }
    closePipePair(_stdinPipe);
    closePipePair(_stdoutPipe);
}


Handler::e_status CGIHandler::handle(const Request &req, Response &res,
                                     const ServerConfig &srv,
                                     const Locations &loc)
{
    static bool sigpipeIgnored = false;
    if (!sigpipeIgnored)
    {
        signal(SIGPIPE, SIG_IGN);
        sigpipeIgnored = true;
    }

    if (req.method != "GET" && req.method != "POST")
    {
        buildError(res, 405);
        res.setHeader("Allow", "GET, POST");
        return (DONE);
    }

    const std::string &root = loc.getRoot().empty() ? srv.getRoot()
                                                    : loc.getRoot();
    std::string scriptPath;
    if (!resolvePath(root, req.path, scriptPath))
    {
        buildError(res, 403);
        return (DONE);
    }

    if (!isScript(scriptPath, loc.getCgiExtensions()))
    {
        buildError(res, 400);
        return (DONE);
    }

    if (!isFile(scriptPath))
    {
        buildError(res, 404);
        return (DONE);
    }

    if (loc.getCgiInterpreter().empty())
    {
        buildError(res, 500);
        return (DONE);
    }

    bool launched = launch(scriptPath, loc.getCgiInterpreter(), req);
    if (!launched)
    {
        buildError(res, 500);
        return (ERROR);
    }

    int timeoutMs = CGI_TIMEOUT_MS;
    {
        const char *t = std::getenv("WEBSERV_CGI_TIMEOUT_MS");
        if (t != NULL && *t != '\0')
            timeoutMs = std::atoi(t);
    }
    time_t deadline = nowMs() + timeoutMs;

    bool fed = writeAllNonblocking(_stdinPipe[1], req.body, deadline);
    closeEnd(_stdinPipe[1]);

    bool timedOut = false;
    bool tooBig = false;
    std::string output = readAllWithDeadline(_stdoutPipe[0], deadline,
                                             timedOut, tooBig);
    closeEnd(_stdoutPipe[0]);

    int status = 0;
    int exitCode = 0;
    for (;;)
    {
        pid_t r = waitpid(_cgiPid, &status, WNOHANG);
        if (r == _cgiPid)
            break;
        if (r < 0)
            break;
        if (nowMs() > deadline)
        {
            timedOut = true;
            kill(_cgiPid, SIGKILL);
            waitpid(_cgiPid, &status, 0);
            break;
        }
        usleep(20000);
    }
    if (WIFEXITED(status))
        exitCode = WEXITSTATUS(status);
    else if (!timedOut)
        exitCode = 1;
    _cgiPid = -1;

    if (timedOut)
    {
        buildError(res, 504);
        return (DONE);
    }
    if (!fed || tooBig || exitCode != 0 || output.empty())
    {
        buildError(res, 502);
        return (DONE);
    }

    res.clear();
    if (!parseCgiOutput(output, res))
    {
        buildError(res, 502);
        return (DONE);
    }
    return (DONE);
}


bool CGIHandler::isScript(const std::string &filePath,
                          const std::vector<std::string> &cgiExtensions) const
{
    if (cgiExtensions.empty())
        return (false);

    size_t dot = filePath.rfind('.');
    if (dot == std::string::npos)
        return (false);
    std::string ext = toLowerCopy(filePath.substr(dot));

    for (size_t i = 0; i < cgiExtensions.size(); ++i)
    {
        std::string want = cgiExtensions[i];
        if (want.empty())
            continue;
        if (want[0] != '.')
            want = "." + want;
        if (ext == toLowerCopy(want))
            return (true);
    }
    return (false);
}


char **CGIHandler::buildEnv(const std::string &interpreter,
                            const std::string &scriptPath,
                            const Request &req)
{
    (void)interpreter;

    _envStore.clear();
    _envStore.push_back("GATEWAY_INTERFACE=CGI/1.1");
    _envStore.push_back("SERVER_SOFTWARE=webserv/1.0");
    _envStore.push_back("SERVER_PROTOCOL=" + req.version);
    _envStore.push_back("SERVER_PORT=");
    _envStore.push_back("REQUEST_METHOD=" + req.method);
    _envStore.push_back("QUERY_STRING=" + req.query);
    _envStore.push_back("PATH_INFO=" + req.path);
    _envStore.push_back("SCRIPT_NAME=" + req.path);
    _envStore.push_back("SCRIPT_FILENAME=" + scriptPath);

    if (!req.body.empty())
    {
        std::ostringstream len;
        len << req.body.size();
        _envStore.push_back("CONTENT_LENGTH=" + len.str());
    }
    std::string ctype = req.getHeader("content-type");
    if (!ctype.empty())
        _envStore.push_back("CONTENT_TYPE=" + ctype);

    std::string ua = req.getHeader("user-agent");
    if (!ua.empty())
        _envStore.push_back("HTTP_USER_AGENT=" + ua);
    std::string accept = req.getHeader("accept");
    if (!accept.empty())
        _envStore.push_back("HTTP_ACCEPT=" + accept);
    std::string cookie = req.getHeader("cookie");
    if (!cookie.empty())
        _envStore.push_back("HTTP_COOKIE=" + cookie);

    char **env = new char *[_envStore.size() + 1];
    for (size_t i = 0; i < _envStore.size(); ++i)
        env[i] = const_cast<char *>(_envStore[i].c_str());
    env[_envStore.size()] = NULL;
    return (env);
}

void CGIHandler::freeEnv(char **env) const
{
    if (env != NULL)
        delete[] env;
}


bool CGIHandler::launch(const std::string &scriptPath,
                        const std::string &interpreter, const Request &req)
{
    closePipePair(_stdinPipe);
    closePipePair(_stdoutPipe);

    if (pipe(_stdinPipe) != 0)
        return (false);
    if (pipe(_stdoutPipe) != 0)
    {
        closePipePair(_stdinPipe);
        return (false);
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        closePipePair(_stdinPipe);
        closePipePair(_stdoutPipe);
        return (false);
    }

    if (pid == 0)
    {
        dup2(_stdinPipe[0], STDIN_FILENO);
        dup2(_stdoutPipe[1], STDOUT_FILENO);

        close(_stdinPipe[0]);
        close(_stdinPipe[1]);
        close(_stdoutPipe[0]);
        close(_stdoutPipe[1]);

        char *argv[3];
        argv[0] = const_cast<char *>(interpreter.c_str());
        argv[1] = const_cast<char *>(scriptPath.c_str());
        argv[2] = NULL;

        char **envp = buildEnv(interpreter, scriptPath, req);
        execve(interpreter.c_str(), argv, envp);
        freeEnv(envp);
        _exit(127);
    }

    _cgiPid = pid;
    closeEnd(_stdinPipe[0]);
    closeEnd(_stdoutPipe[1]);

    fcntl(_stdinPipe[1], F_SETFL, O_NONBLOCK);
    fcntl(_stdoutPipe[0], F_SETFL, O_NONBLOCK);
    return (true);
}


pid_t CGIHandler::getCgiPid() const
{
    return (_cgiPid);
}

int CGIHandler::getStdinFd() const
{
    return (_stdinPipe[1]);
}

int CGIHandler::getStdoutFd() const
{
    return (_stdoutPipe[0]);
}

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


static const int    CGI_TIMEOUT_MS = 5000;
static const size_t CGI_MAX_OUTPUT = 128u * 1024u * 1024u;

static time_t nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (static_cast<time_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000);
}

static void closeEnd(int &fd)
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

static void closePipePair(int *p)
{
    closeEnd(p[0]);
    closeEnd(p[1]);
}

static std::string toLowerCopy(const std::string &s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(out[i])));
    return (out);
}

static std::string trimCopy(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return ("");
    size_t e = s.find_last_not_of(" \t");
    return (s.substr(b, e - b + 1));
}

// A CGI can write output before reading all its input.
static bool communicateCgi(int &inputFd, int &outputFd,
                           const std::string &input, std::string &output,
                           time_t deadline, bool &timedOut, bool &tooBig)
{
    size_t sent = 0;
    char buffer[65536];
    while (inputFd >= 0 || outputFd >= 0)
    {
        if (inputFd >= 0 && sent == input.size())
            closeEnd(inputFd);
        if (inputFd < 0 && outputFd < 0)
            break;
        const int left = static_cast<int>(deadline - nowMs());
        if (left <= 0)
        {
            timedOut = true;
            return false;
        }
        struct pollfd fds[2];
        fds[0].fd = inputFd;
        fds[0].events = POLLOUT;
        fds[0].revents = 0;
        fds[1].fd = outputFd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        const int ready = poll(fds, 2, left);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
        {
            timedOut = ready == 0;
            return false;
        }
        if (fds[1].revents & (POLLIN | POLLHUP))
        {
            const ssize_t n = read(outputFd, buffer, sizeof(buffer));
            if (n > 0)
            {
                if (static_cast<size_t>(n) > CGI_MAX_OUTPUT - output.size())
                {
                    tooBig = true;
                    return false;
                }
                output.append(buffer, static_cast<size_t>(n));
            }
            else if (n == 0)
                closeEnd(outputFd);
            else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                return false;
        }
        if (fds[1].revents & (POLLERR | POLLNVAL))
            return false;
        if (fds[0].revents & POLLOUT)
        {
            const size_t remaining = input.size() - sent;
            const size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            const ssize_t n = write(inputFd, input.data() + sent, count);
            if (n > 0)
                sent += static_cast<size_t>(n);
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                return false;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
            return false;
    }
    return sent == input.size();
}


static bool parseCgiOutput(const std::string &raw, Response &res)
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

    std::vector<std::string> cgiExts;
    if (!loc.getCgiExtension().empty())
    {
        cgiExts.push_back(loc.getCgiExtension());
    }
    if (!isScript(scriptPath, cgiExts))
    {
        buildError(res, 400);
        return (DONE);
    }

    if (!isFile(scriptPath))
    {
        buildError(res, 404);
        return (DONE);
    }

    // Use first part of cgi_path as interpreter, or default to the script extension
    const std::string interpreter = loc.getCgiPath();
    if (interpreter.empty())
    {
        buildError(res, 500);
        return (DONE);
    }

    bool launched = launch(scriptPath, interpreter, req);
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

    bool timedOut = false;
    bool tooBig = false;
    std::string output;
    bool fed = communicateCgi(_stdinPipe[1], _stdoutPipe[0], req.body,
                              output, deadline, timedOut, tooBig);
    closeEnd(_stdinPipe[1]);
    closeEnd(_stdoutPipe[0]);
    if (!fed)
        kill(_cgiPid, SIGKILL);

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
    _envStore.push_back("REQUEST_URI=" + req.path
                        + (req.query.empty() ? "" : "?" + req.query));
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

    for (std::map<std::string, std::string>::const_iterator it = req.headers.begin();
         it != req.headers.end(); ++it)
    {
        const std::string name = toLowerCopy(it->first);
        if (name == "content-length" || name == "content-type")
            continue;
        std::string environmentName = "HTTP_";
        for (size_t i = 0; i < name.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(name[i]);
            environmentName += character == '-' ? '_'
                : static_cast<char>(std::toupper(character));
        }
        _envStore.push_back(environmentName + "=" + it->second);
    }

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

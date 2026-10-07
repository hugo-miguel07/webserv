#include "RedirectHandler.hpp"
#include <sstream>


RedirectHandler::RedirectHandler() {}

RedirectHandler::~RedirectHandler() {}

Handler::e_status RedirectHandler::handle(const Request &req, Response &res,
                                          const ServerConfig &srv,
                                          const Locations &loc)
{
    (void)srv;

    const std::string &target = loc.getRedirect();
    int code = loc.getRedirectCode();

    if (target.empty() || code == 0)
    {
        buildError(res, 500);
        return (DONE);
    }

    if (code < 300 || code > 399)
    {
        buildError(res, 500);
        return (DONE);
    }

    res.clear();
    res.setStatus(code, Response::getReasonPhrase(code));
    res.setHeader("Location", buildTarget(req, target));

    if (req.method != "HEAD")
    {
        std::ostringstream body;
        body << "<html>\r\n<head><title>" << code << " "
             << Response::getReasonPhrase(code) << "</title></head>\r\n"
             << "<body>\r\n<center><h1>" << code << " "
             << Response::getReasonPhrase(code) << "</h1></center>\r\n"
             << "<hr><center>webserv</center>\r\n</body>\r\n</html>\r\n";
        res.setBody(body.str());
        res.setHeader("Content-Type", "text/html");
    }

    return (DONE);
}

std::string RedirectHandler::buildTarget(const Request &req,
                                         const std::string &redirectConfig) const
{
    static const std::string schemeSep = "://";

    if (redirectConfig.find(schemeSep) != std::string::npos)
        return (redirectConfig);

    std::string host = req.getHeader("host");
    if (!host.empty())
        return ("http://" + host + redirectConfig);
    return (redirectConfig);
}

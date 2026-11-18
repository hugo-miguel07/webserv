#include "Request.hpp"
#include <iostream>
#include <limits>
#include <sstream>

/* c++ -Wall -Wextra -Werror -std=c++98 -Iincludes test/request_test.cpp \
       srcs/Request.cpp srcs/Client.cpp srcs/ServerConfig.cpp srcs/Locations.cpp \
       srcs/trim.cpp -o /tmp/request_test && /tmp/request_test
*/

static unsigned int total = 0;
static unsigned int failures = 0;

static void testRequest(const char *name, const std::vector<ServerConfig> &servers,
                        const std::string &wire, const std::string &expectedState,
                        const std::string &expectedBody = "",
                        const std::string &expectedQuery = "")
{
    std::ostringstream debug;
    std::streambuf *previous = std::cout.rdbuf(debug.rdbuf());
    std::string state;
    std::string detail;
    bool dataMatches = true;
    try
    {
        Request request(servers);
        bool complete = request.checkHeader(wire);
        if (complete)
        {
            request.HeadersToMap(wire);
            request.readingBody(wire);
            if (request.getHasContentStatus())
                complete = request.checkingBody_framing(wire);
            else if (request.getTransferEncodingStatus())
                complete = request.checkingBody_chuncked(wire);
        }
        if (!complete)
            state = "WAIT";
        else
        {
            request.start_parsing(wire);
            state = "PARSED";
            dataMatches = !request.method.empty() && !request.path.empty()
                && request.version == "HTTP/1.1" && request.headers.count("host") == 1
                && request.getServerConfig().getRoot() == "www"
                && request.body == expectedBody && request.query == expectedQuery;
        }
    }
    catch (int statusCode)
    {
        std::ostringstream code;
        code << statusCode;
        state = code.str();
    }
    catch (const std::exception &error)
    {
        state = "REJECT";
        detail = error.what();
    }
    std::cout.rdbuf(previous);
    ++total;
    const bool ok = state == expectedState && dataMatches;
    if (!ok)
        ++failures;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name;
    if (!ok)
        std::cout << " expected=" << expectedState << " actual=" << state
                  << " dataMatches=" << dataMatches << " " << detail;
    std::cout << '\n';
}

int main()
{
    ServerConfig server;
    server.setServerName("localhost");
    server.setRoot("www");
    Locations root;
    root.setPath("/");
    root.setAllowedMethod("GET");
    root.setAllowedMethod("POST");
    server.addLocation(root);
    Locations upload;
    upload.setPath("/upload");
    upload.setAllowedMethod("POST");
    server.addLocation(upload);
    Locations admin;
    admin.setPath("/admin");
    admin.setAllowedMethod("DELETE");
    server.addLocation(admin);
    Locations redirect;
    redirect.setPath("/old");
    redirect.setReturn(301, "/new");
    server.addLocation(redirect);
    const std::vector<ServerConfig> servers(1, server);
    const std::string post = "POST /upload HTTP/1.1\r\nHost: localhost\r\n";
    const std::string chunk = post + "Transfer-Encoding: chunked\r\n\r\n";

    testRequest("GET publishes handler data", servers, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", "PARSED");
    testRequest("incomplete headers wait", servers, post + "Content-Len", "WAIT");
    testRequest("POST body", servers, post + "Content-Length: 5\r\n\r\nhello", "PARSED", "hello");
    testRequest("binary body", servers, post + "Content-Length: 5\r\n\r\n" + std::string("a\0b\r\n", 5), "PARSED", std::string("a\0b\r\n", 5));
    testRequest("incomplete body waits", servers, post + "Content-Length: 5\r\n\r\nhe", "WAIT");
    testRequest("lowercase incomplete body waits", servers, post + "content-length: 5\r\n\r\nhe", "WAIT");
    testRequest("lowercase complete body", servers, post + "content-length: 5\r\n\r\nhello", "PARSED", "hello");
    testRequest("no space after colon", servers, post + "Content-Length:5\r\n\r\nhello", "PARSED", "hello");
    testRequest("tab after colon", servers, post + "Content-Length:\t5\r\n\r\nhello", "PARSED", "hello");
    testRequest("invalid Content-Length", servers, post + "Content-Length: abc\r\n\r\n", "REJECT");
    testRequest("empty Content-Length", servers, post + "Content-Length:\r\n\r\n", "REJECT");
    testRequest("conflicting lengths", servers, post + "Content-Length: 5\r\nContent-Length: 99\r\n\r\nhello", "REJECT");
    testRequest("duplicate lengths with different case", servers, post + "Content-Length: 5\r\ncontent-length: 5\r\n\r\nhello", "REJECT");
    testRequest("keyword in unrelated header", servers, "GET / HTTP/1.1\r\nHost: localhost\r\nX-Note: Content-Length\r\n\r\n", "PARSED");
    testRequest("keyword inside fixed body", servers, post + "Content-Length: 17\r\n\r\nTransfer-Encoding", "PARSED", "Transfer-Encoding");
    testRequest("mixed case Host header", servers, "GET / HTTP/1.1\r\nHoSt: localhost\r\n\r\n", "PARSED");
    testRequest("mixed case hostname", servers, "GET / HTTP/1.1\r\nHost: LOCALHOST:8081\r\n\r\n", "PARSED");
    testRequest("duplicate Host", servers, "GET / HTTP/1.1\r\nHost: localhost\r\nhost: localhost\r\n\r\n", "REJECT");
    testRequest("empty query", servers, "GET /? HTTP/1.1\r\nHost: localhost\r\n\r\n", "PARSED");
    testRequest("nonempty query", servers, "GET /cgi-bin/hello.py?a=1 HTTP/1.1\r\nHost: localhost\r\n\r\n", "PARSED", "", "a=1");
    testRequest("newline in target rejected", servers, "GET /bad\nname HTTP/1.1\r\nHost: localhost\r\n\r\n", "REJECT");
    testRequest("DELETE uses specific location", servers, "DELETE /admin/file.txt HTTP/1.1\r\nHost: localhost\r\n\r\n", "PARSED");
    testRequest("GET prohibited by upload location", servers, "GET /upload HTTP/1.1\r\nHost: localhost\r\n\r\n", "405");
    testRequest("parent location prohibits DELETE", servers, "DELETE /file.txt HTTP/1.1\r\nHost: localhost\r\n\r\n", "405");
    testRequest("location prefix respects path boundary", servers, "GET /upload.html HTTP/1.1\r\nHost: localhost\r\n\r\n", "PARSED");
    testRequest("location without method restriction", servers, "GET /old HTTP/1.1\r\nHost: localhost\r\n\r\n", "PARSED");
    ServerConfig noRoot;
    noRoot.setServerName("localhost");
    noRoot.setRoot("www");
    noRoot.addLocation(admin);
    testRequest("missing location produces 404", std::vector<ServerConfig>(1, noRoot), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", "404");
    testRequest("chunked body", servers, chunk + "5\r\nhello\r\n0\r\n\r\n", "PARSED", "hello");
    testRequest("incomplete chunk waits", servers, chunk + "5\r\nhe", "WAIT");
    testRequest("chunk data with header keyword", servers, chunk + "e\r\nContent-Length\r\n0\r\n\r\n", "PARSED", "Content-Length");
    testRequest("nonhex chunk suffix", servers, chunk + "1z\r\na\r\n0\r\n\r\n", "REJECT");
    testRequest("negative chunk size", servers, chunk + "-1\r\n", "REJECT");
    testRequest("chunk size overflow", servers, chunk + std::string(40, 'f') + "\r\n", "REJECT");
    std::ostringstream maxSize;
    maxSize << std::hex << std::numeric_limits<size_t>::max();
    testRequest("huge chunk does not wrap offsets", servers, chunk + maxSize.str() + "\r\n", "WAIT");
    testRequest("chunk trailers", servers, chunk + "1\r\na\r\n0\r\nX-Trailer: value\r\n\r\n", "PARSED", "a");
    testRequest("fragmented trailer waits", servers, chunk + "1\r\na\r\n0\r\nX-Trailer: value\r\n", "WAIT");
    testRequest("missing trailer separator waits", servers, chunk + "0\r\n", "WAIT");
    testRequest("framing trailer rejected", servers, chunk + "0\r\nContent-Length: 5\r\n\r\n", "REJECT");
    testRequest("chunk extensions", servers, chunk + "1;foo=bar\r\na\r\n0\r\n\r\n", "PARSED", "a");
    testRequest("mixed case transfer coding", servers, post + "tRaNsFeR-EnCoDiNg: CHUNKED\r\n\r\n1\r\na\r\n0\r\n\r\n", "PARSED", "a");
    testRequest("duplicate transfer coding", servers, post + "Transfer-Encoding: chunked\r\ntransfer-encoding: chunked\r\n\r\n", "REJECT");
    testRequest("length plus transfer coding", servers, post + "Content-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", "REJECT");
    std::cout << "Parser tests: " << total - failures << "/" << total << " passed\n";
    return failures ? 1 : 0;
}

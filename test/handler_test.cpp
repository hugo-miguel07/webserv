#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>
#include <unistd.h>

#include "StaticHandler.hpp"
#include "DirectoryListingHandler.hpp"
#include "RedirectHandler.hpp"
#include "UploadHandler.hpp"
#include "DeleteHandler.hpp"
#include "CGIHandler.hpp"

/*
    Standalone regression tests for the implemented handlers.
    NOT part of the webserv build: compile with

    c++ -Wall -Wextra -Werror -std=c++98 -Iincludes test/handler_test.cpp \
        srcs/Locations.cpp srcs/Handler.cpp srcs/Request.cpp srcs/Response.cpp \
        srcs/ServerConfig.cpp srcs/StaticHandler.cpp \
        srcs/DirectoryListingHandler.cpp srcs/RedirectHandler.cpp \
        srcs/UploadHandler.cpp srcs/DeleteHandler.cpp srcs/CGIHandler.cpp \
        -o /tmp/handler_test && /tmp/handler_test
*/

static int g_fail = 0;

static void expect(bool ok, const std::string &name)
{
    if (ok)
        std::cout << "[PASS] " << name << "\n";
    else
    {
        std::cout << "[FAIL] " << name << "\n";
        ++g_fail;
    }
}

static bool contains(const std::string &hay, const std::string &needle)
{
    return hay.find(needle) != std::string::npos;
}

static void writeFile(const std::string &path, const std::string &content)
{
    std::ofstream f(path.c_str(), std::ios::binary);
    f << content;
}

static std::string readFile(const std::string &path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

static bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static std::string numberToString(size_t n)
{
    std::ostringstream out;
    out << n;
    return out.str();
}

static ServerConfig makeServer(const std::string &root)
{
    ServerConfig srv;
    srv.setRoot(root);
    srv.setIndex("index.html");
    srv.setBodySize(1024 * 1024);
    return srv;
}

static Locations makeLoc(const std::string &path)
{
    Locations loc;
    loc.setPath(path);
    return loc;
}

static Request makeReq(const std::string &method, const std::string &path,
                       const std::string &host)
{
    Request req;
    req.method = method;
    req.path = path;
    req.version = "HTTP/1.1";
    if (!host.empty())
        req.headers["host"] = host;
    return req;
}

int main()
{
    // ---- fixture: a disposable tree in /tmp -----------------------------
    std::string root = "/tmp/ws_test_root";
    std::string cmd = "rm -rf " + root;
    std::system(cmd.c_str());
    mkdir(root.c_str(), 0755);
    mkdir((root + "/sub").c_str(), 0755);
    mkdir((root + "/emptydir").c_str(), 0755);
    writeFile(root + "/file.txt", "hello static");
    writeFile(root + "/index.html", "<h1>server index</h1>");
    writeFile(root + "/sub/index.html", "<h1>sub index</h1>");
    writeFile(root + "/sub/data.bin", "\x01\x02binary");
    writeFile(root + "/sub/with space.txt", "spacey");
    writeFile(root + "/secret.txt", "top secret");

    ServerConfig srv = makeServer(root);
    StaticHandler staticHandler;
    DirectoryListingHandler lister;
    RedirectHandler redirecter;
    UploadHandler uploader;

    // ---------------- RedirectHandler ---------------------------------
    {
        Locations loc = makeLoc("/old");
        loc.setRedirect("https://example.com", 301);

        Response res;
        redirecter.handle(makeReq("GET", "/old/page", "site1.com:8080"),
                          res, srv, loc);
        expect(res.statusCode == 301, "redirect: 301 status");
        expect(res.headers.count("Location") == 1 &&
               res.headers["Location"] == "https://example.com",
               "redirect: absolute target passed through");
        expect(contains(res.body, "Moved Permanently"),
               "redirect: body mentions reason phrase");
        expect(res.headers["Content-Type"] == "text/html",
               "redirect: html content type");

        Locations rel = makeLoc("/old");
        rel.setRedirect("/new", 302);
        redirecter.handle(makeReq("GET", "/old/page", "site1.com:8080"),
                          res, srv, rel);
        expect(res.statusCode == 302 &&
               res.headers["Location"] == "http://site1.com:8080/new",
               "redirect: relative target prefixed with request host");

        Locations head = makeLoc("/old");
        head.setRedirect("/new", 301);
        redirecter.handle(makeReq("HEAD", "/old", ""), res, srv, head);
        expect(res.statusCode == 301 && res.body.empty(),
               "redirect: HEAD has Location but no body");

        Locations bad = makeLoc("/old");
        bad.setRedirect("/new", 200);
        redirecter.handle(makeReq("GET", "/old", ""), res, srv, bad);
        expect(res.statusCode == 500,
               "redirect: non-3xx configured code -> 500");

        Locations unset = makeLoc("/old");
        redirecter.handle(makeReq("GET", "/old", ""), res, srv, unset);
        expect(res.statusCode == 500, "redirect: no config -> 500");
    }

    // ---------------- StaticHandler ------------------------------------
    {
        Response res;
        staticHandler.handle(makeReq("GET", "/file.txt", ""), res, srv,
                             makeLoc("/"));
        expect(res.statusCode == 200 && res.body == "hello static",
               "static: plain file served");
        expect(res.headers["Content-Type"] == "text/plain",
               "static: content type by extension");

        staticHandler.handle(makeReq("GET", "/sub/data.bin", ""), res, srv,
                             makeLoc("/"));
        expect(res.statusCode == 200 && res.body == std::string("\x01\x02binary"),
               "static: binary file intact");
        expect(res.headers["Content-Type"] == "application/octet-stream",
               "static: unknown extension -> octet-stream");

        staticHandler.handle(makeReq("GET", "/", ""), res, srv, makeLoc("/"));
        expect(res.statusCode == 200 && res.body == "<h1>server index</h1>",
               "static: index served for /");

        staticHandler.handle(makeReq("GET", "/sub", ""), res, srv, makeLoc("/"));
        expect(res.statusCode == 301 && res.headers["Location"] == "/sub/",
               "static: directory without slash -> 301");

        // regression: the query string must survive the directory redirect
        Request subQuery = makeReq("GET", "/sub", "");
        subQuery.query = "page=2";
        staticHandler.handle(subQuery, res, srv, makeLoc("/"));
        expect(res.statusCode == 301 &&
               res.headers["Location"] == "/sub/?page=2",
               "static: directory 301 preserves query string");

        staticHandler.handle(makeReq("GET", "/sub/", ""), res, srv, makeLoc("/"));
        expect(res.statusCode == 200 && res.body == "<h1>sub index</h1>",
               "static: index served for slashed dir");

        Locations autoOn = makeLoc("/emptydir");
        autoOn.setAutoindex(true);
        staticHandler.handle(makeReq("GET", "/emptydir/", ""), res, srv, autoOn);
        expect(res.statusCode == 200 && contains(res.body, "Index of /emptydir/"),
               "static: autoindex on -> listing");

        staticHandler.handle(makeReq("GET", "/emptydir/", ""), res, srv,
                             makeLoc("/emptydir"));
        expect(res.statusCode == 403, "static: autoindex off, no index -> 403");

        staticHandler.handle(makeReq("GET", "/missing.txt", ""), res, srv,
                             makeLoc("/"));
        expect(res.statusCode == 404, "static: missing file -> 404");

        staticHandler.handle(makeReq("GET", "/../secret.txt", ""), res, srv,
                             makeLoc("/"));
        expect(res.statusCode == 403, "static: traversal -> 403");

        staticHandler.handle(makeReq("GET", "/sub/../file.txt", ""), res, srv,
                             makeLoc("/"));
        expect(res.statusCode == 200 && res.body == "hello static",
               "static: legit .. inside root collapses");

        staticHandler.handle(makeReq("POST", "/file.txt", ""), res, srv,
                             makeLoc("/"));
        expect(res.statusCode == 405 && res.headers["Allow"] == "GET, HEAD",
               "static: POST (empty list) -> 405 Allow GET, HEAD (nginx parity)");

        Locations postOnly = makeLoc("/sub");
        postOnly.addAllowedMethod("POST");
        staticHandler.handle(makeReq("GET", "/file.txt", ""), res, srv,
                             postOnly);
        expect(res.statusCode == 405 && res.headers["Allow"] == "POST",
               "static: GET on POST-only location -> 405 Allow POST");

        Locations methods = makeLoc("/sub");
        methods.addAllowedMethod("GET");
        methods.addAllowedMethod("HEAD");
        staticHandler.handle(makeReq("DELETE", "/file.txt", ""), res, srv,
                             methods);
        expect(res.statusCode == 405 && res.headers["Allow"] == "GET, HEAD",
               "static: DELETE on GET/HEAD location -> 405 Allow GET, HEAD");

        staticHandler.handle(makeReq("HEAD", "/file.txt", ""), res, srv,
                             makeLoc("/"));
        std::string lenHead = res.headers["Content-Length"];
        bool headOk = res.statusCode == 200 && res.body.empty() &&
                      lenHead == "12";
        expect(headOk, "static: HEAD no body but GET's Content-Length");

        staticHandler.handle(makeReq("GET", "/file.txt", ""), res, srv,
                             makeLoc("/"));
        expect(res.body == "hello static" && lenHead == "12",
               "static: GET after HEAD body/length cross-check");
    }

    // ---------------- root override + listing details -------------------
    {
        Locations loc = makeLoc("/sub");
        loc.setRoot(root + "/sub");
        Response res;
        staticHandler.handle(makeReq("GET", "/index.html", ""), res, srv, loc);
        expect(res.statusCode == 200 && res.body == "<h1>sub index</h1>",
               "static: location root override wins");

        staticHandler.handle(makeReq("GET", "/data.bin", ""), res, srv, loc);
        expect(res.statusCode == 200 && res.body == std::string("\x01\x02binary"),
               "static: root override serves relocated file");

        std::string dir = root + "/sub";
        lister.serve(res, dir, "/sub");
        expect(res.statusCode == 200 &&
               contains(res.body, ">data.bin<") &&
               contains(res.body, ">index.html<") &&
               contains(res.body, ">with space.txt<") &&
               res.body.find(">data.bin<") < res.body.find(">with space.txt<"),
               "listing: entries sorted and rendered");

        expect(contains(res.body, "with%20space.txt"),
               "listing: hrefs percent-encode spaces");
        expect(!contains(res.body, "href=\"/tmp"), "listing: no disk paths leak");

        // regression: '/' in the url path must stay literal in hrefs —
        // encoding the whole path used to produce href="%2Fsub/..."
        expect(contains(res.body, "href=\"/sub/data.bin\"") &&
               contains(res.body, "href=\"/sub/with%20space.txt\""),
               "listing: hrefs keep '/' literal (no %2F)");

        // regression: root listing must not %2F-encode the leading '/'
        lister.serve(res, root, "/");
        expect(contains(res.body, "href=\"/file.txt\""),
               "listing: root listing href starts at /");

        // HTML escaping: hostile names / url path render as text, not markup.
        // (NB: fixtures com tags emparelhadas tipo <b>x</b> nem sempre sao
        // criaveis em todos os filesystems/sandboxes, por isso usamos <img> —
        // prova o mesmo: nenhum '<' cru chega ao HTML)
        std::string xssDir = root + "/xss";
        mkdir(xssDir.c_str(), 0755);
        writeFile(xssDir + "/<img>x.txt", "x");
        writeFile(xssDir + "/a&b\"c.txt", "x");
        lister.serve(res, xssDir, "/xss/");
        expect(!contains(res.body, "<img>x") &&
               contains(res.body, "&lt;img&gt;x.txt"),
               "listing: hostile file name is inert text");
        expect(contains(res.body, "a&amp;b&quot;c.txt"),
               "listing: & and \" escaped in entry names");
        lister.serve(res, xssDir, "/<script>/x&y");
        expect(!contains(res.body, "Index of /<script>") &&
               contains(res.body, "Index of /&lt;script&gt;/x&amp;y"),
               "listing: url path escaped in title and h1");
    }

    // ---------------- UploadHandler -----------------------------------
    {
        Response res;
        std::string store = root + "/uploads";
        mkdir(store.c_str(), 0755);

        Locations up = makeLoc("/upload");
        up.addAllowedMethod("POST");
        up.setUploadStore(store);

        Request post = makeReq("POST", "/upload", "");
        post.body = "raw file content";
        post.headers["content-length"] = "16";

        uploader.handle(post, res, srv, up);
        expect(res.statusCode == 201 &&
               res.headers["Location"] == store + "/upload",
               "upload: raw body 201 + Location");
        expect(readFile(store + "/upload") == "raw file content",
               "upload: raw body saved verbatim");

        // multipart with two files + one plain field
        std::string body =
            "--BOUND\r\n"
            "Content-Disposition: form-data; name=\"f1\"; filename=\"a.txt\"\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "alpha content\r\n"
            "--BOUND\r\n"
            "Content-Disposition: form-data; name=\"f2\"; filename=\"b.bin\"\r\n"
            "\r\n"
            "\x01" "\x02" "binary\r\n"
            "--BOUND\r\n"
            "Content-Disposition: form-data; name=\"note\"\r\n"
            "\r\n"
            "just a field\r\n"
            "--BOUND--\r\n";
        Request mp = makeReq("POST", "/upload", "");
        mp.body = body;
        mp.headers["content-type"] = "multipart/form-data; boundary=BOUND";
        mp.headers["content-length"] = numberToString(body.size());

        uploader.handle(mp, res, srv, up);
        expect(res.statusCode == 201, "upload: multipart 201");
        expect(readFile(store + "/a.txt") == "alpha content",
               "upload: part 1 saved without trailing CRLF");
        expect(readFile(store + "/b.bin") == std::string("\x01" "\x02" "binary"),
               "upload: binary part verbatim");
        expect(!fileExists(store + "/note"),
               "upload: plain form field not saved");

        // Windows-style client path neutralized to basename
        std::string body2 =
            "--BOUND\r\n"
            "Content-Disposition: form-data; name=\"f\"; filename=\"C:\\\\fakepath\\\\win.txt\"\r\n"
            "\r\n"
            "win\r\n"
            "--BOUND--\r\n";
        Request mp2 = makeReq("POST", "/upload", "");
        mp2.body = body2;
        mp2.headers["content-type"] = "multipart/form-data; boundary=BOUND";
        uploader.handle(mp2, res, srv, up);
        expect(res.statusCode == 201 && readFile(store + "/win.txt") == "win",
               "upload: C:\\fakepath basename kept");

        // traversal attempt in a part filename -> 400, nothing written
        std::string body3 =
            "--BOUND\r\n"
            "Content-Disposition: form-data; name=\"f\"; filename=\"../../evil.txt\"\r\n"
            "\r\n"
            "evil\r\n"
            "--BOUND--\r\n";
        Request mp3 = makeReq("POST", "/upload", "");
        mp3.body = body3;
        mp3.headers["content-type"] = "multipart/form-data; boundary=BOUND";
        uploader.handle(mp3, res, srv, up);
        expect(res.statusCode == 400 && !fileExists(root + "/evil.txt"),
               "upload: traversal filename -> 400, nothing escaped");

        // no boundary in Content-Type -> falls back to raw mode; empty URL
        // segment -> 400
        Request noB = makeReq("POST", "/upload/", "");
        noB.body = "x";
        noB.headers["content-type"] = "multipart/form-data";   // no boundary=
        uploader.handle(noB, res, srv, up);
        expect(res.statusCode == 400, "upload: no boundary + empty name -> 400");

        // truncated multipart -> 400
        Request trunc = makeReq("POST", "/upload", "");
        trunc.body = "--BOUND\r\nContent-Disposition: form-data; filename=\"x\"\r\n";
        trunc.headers["content-type"] = "multipart/form-data; boundary=BOUND";
        uploader.handle(trunc, res, srv, up);
        expect(res.statusCode == 400, "upload: truncated multipart -> 400");

        // size limit: 413 with small limit; fail-closed with no limit
        srv.setBodySize(8);
        Request big = makeReq("POST", "/upload", "");
        big.body = "1234567890";
        uploader.handle(big, res, srv, up);
        expect(res.statusCode == 413, "upload: body over limit -> 413");

        srv.setBodySize(0);                     // no limit configured
        Request any = makeReq("POST", "/upload", "");
        any.body = "123";
        uploader.handle(any, res, srv, up);
        expect(res.statusCode == 413,
               "upload: missing limit fails closed -> 413");

        srv.setBodySize(1024 * 1024);           // restore

        // no upload_store -> 500
        Locations noStore = makeLoc("/upload");
        Request r2 = makeReq("POST", "/upload", "");
        r2.body = "x";
        uploader.handle(r2, res, srv, noStore);
        expect(res.statusCode == 500, "upload: no upload_store -> 500");

        // method: GET on POST-only location -> 405 with Allow: POST
        uploader.handle(makeReq("GET", "/upload", ""), res, srv, up);
        expect(res.statusCode == 405 && res.headers["Allow"] == "POST",
               "upload: GET on POST-only location -> 405 Allow POST");

        // DELETE passes location policy (empty list elsewhere) but POST is
        // the only capability -> 405 Allow POST
        uploader.handle(makeReq("DELETE", "/upload", ""), res, srv,
                        makeLoc("/upload"));
        expect(res.statusCode == 405 && res.headers["Allow"] == "POST",
               "upload: DELETE (empty list) -> 405 Allow POST");
    }

    // ---------------- DeleteHandler ------------------------------------
    {
        Response res;
        DeleteHandler deleter;

        writeFile(root + "/deleteme.txt", "bye");
        mkdir((root + "/emptydir2").c_str(), 0755);
        mkdir((root + "/fulldir").c_str(), 0755);
        writeFile(root + "/fulldir/keep.txt", "still here");
        writeFile(root + "/secret2.txt", "nope");

        Locations allowDel = makeLoc("/d");
        allowDel.addAllowedMethod("DELETE");
        allowDel.addAllowedMethod("GET");

        deleter.handle(makeReq("DELETE", "/deleteme.txt", ""), res, srv,
                       allowDel);
        expect(res.statusCode == 204, "delete: file -> 204");
        expect(!fileExists(root + "/deleteme.txt"),
               "delete: file really gone");

        deleter.handle(makeReq("DELETE", "/emptydir2", ""), res, srv,
                       allowDel);
        expect(res.statusCode == 204, "delete: empty dir -> 204");
        expect(!fileExists(root + "/emptydir2"),
               "delete: empty dir really gone");

        deleter.handle(makeReq("DELETE", "/fulldir", ""), res, srv, allowDel);
        expect(res.statusCode == 409, "delete: non-empty dir -> 409");
        expect(fileExists(root + "/fulldir/keep.txt"),
               "delete: children intact after 409");

        deleter.handle(makeReq("DELETE", "/missing.txt", ""), res, srv,
                       allowDel);
        expect(res.statusCode == 404, "delete: missing -> 404");

        deleter.handle(makeReq("DELETE", "/../secret2.txt", ""), res, srv,
                       allowDel);
        expect(res.statusCode == 403, "delete: traversal -> 403");
        expect(fileExists(root + "/secret2.txt"),
               "delete: traversal target intact");

        Locations getOnly = makeLoc("/d");
        getOnly.addAllowedMethod("GET");
        writeFile(root + "/protected.txt", "safe");
        deleter.handle(makeReq("DELETE", "/protected.txt", ""), res, srv,
                       getOnly);
        expect(res.statusCode == 405 && res.headers["Allow"] == "GET",
               "delete: not in allow_methods -> 405 Allow GET");
        expect(fileExists(root + "/protected.txt"),
               "delete: file untouched after 405");

        writeFile(root + "/protected2.txt", "safe too");
        deleter.handle(makeReq("DELETE", "/protected2.txt", ""), res, srv,
                       makeLoc("/d"));
        expect(res.statusCode == 405 && res.headers["Allow"] == "DELETE",
               "delete: empty method list fails closed -> 405 Allow DELETE");
        expect(fileExists(root + "/protected2.txt"),
               "delete: file untouched (empty list)");

        Locations delOnly = makeLoc("/d");
        delOnly.addAllowedMethod("DELETE");
        deleter.handle(makeReq("GET", "/protected2.txt", ""), res, srv,
                       delOnly);
        expect(res.statusCode == 405 && res.headers["Allow"] == "DELETE",
               "delete: GET on DELETE-only -> 405 Allow DELETE");
    }

    // ---------------- CGIHandler ---------------------------------------
    {
        Response res;
        CGIHandler cgi;

        std::string cgiDir = root + "/cgi-bin";
        mkdir(cgiDir.c_str(), 0755);
        writeFile(cgiDir + "/ok.py",
                  "#!/usr/bin/env python3\n"
                  "print('Content-Type: text/html')\n"
                  "print()\n"
                  "print('<h1>hi</h1>')\n");
        writeFile(cgiDir + "/status.py",
                  "#!/usr/bin/env python3\n"
                  "print('Status: 404 Not Found')\n"
                  "print('Content-Type: text/plain')\n"
                  "print()\n"
                  "print('custom not found')\n");
        writeFile(cgiDir + "/loc.py",
                  "#!/usr/bin/env python3\n"
                  "print('Location: /elsewhere')\n"
                  "print('Content-Type: text/plain')\n"
                  "print()\n");
        writeFile(cgiDir + "/fail.py",
                  "#!/usr/bin/env python3\n"
                  "import sys\n"
                  "sys.exit(3)\n");
        writeFile(cgiDir + "/malformed.py",
                  "#!/usr/bin/env python3\n"
                  "print('just some text, no headers')\n");
        writeFile(cgiDir + "/loop.py",
                  "#!/usr/bin/env python3\n"
                  "import time\n"
                  "time.sleep(60)\n");
        writeFile(cgiDir + "/post.py",
                  "#!/usr/bin/env python3\n"
                  "import sys, os\n"
                  "n = int(os.environ.get('CONTENT_LENGTH', '0'))\n"
                  "data = sys.stdin.read(n)\n"
                  "print('Content-Type: text/plain')\n"
                  "print()\n"
                  "print('len=' + str(len(data)) + ' body=' + data)\n");
        writeFile(cgiDir + "/query.py",
                  "#!/usr/bin/env python3\n"
                  "import os\n"
                  "print('Content-Type: text/plain')\n"
                  "print()\n"
                  "print('qs=' + os.environ.get('QUERY_STRING', ''))\n");
        std::system(("chmod +x " + cgiDir + "/*.py").c_str());

        Locations cgiLoc = makeLoc("/cgi-bin");
        cgiLoc.setRoot(cgiDir);
        cgiLoc.setCgiInterpreter("/usr/bin/python3");
        cgiLoc.addCgiExtension(".py");

        // 1. simple success
        cgi.handle(makeReq("GET", "/ok.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 200 && res.body.find("<h1>hi</h1>") !=
               std::string::npos,
               "cgi: 200 + body parsed");
        expect(res.headers["Content-Type"] == "text/html",
               "cgi: Content-Type honored");

        // 2. Status: override
        cgi.handle(makeReq("GET", "/status.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 404 && res.body.find("custom not found") !=
               std::string::npos,
               "cgi: Status: 404 honored with body");

        // 3. Location (client redirect via CGI)
        cgi.handle(makeReq("GET", "/loc.py", ""), res, srv, cgiLoc);
        expect(res.headers.count("Location") == 1 &&
               res.headers["Location"] == "/elsewhere",
               "cgi: Location passthrough");

        // 4. POST: body piped through stdin + CONTENT_LENGTH
        Request post = makeReq("POST", "/post.py", "");
        post.body = "hello cgi";
        post.headers["content-length"] = "9";
        cgi.handle(post, res, srv, cgiLoc);
        expect(res.statusCode == 200 &&
               res.body.find("len=9 body=hello cgi") != std::string::npos,
               "cgi: POST body via stdin + CONTENT_LENGTH");

        // 5. QUERY_STRING
        Request q = makeReq("GET", "/query.py", "");
        q.query = "a=1&b=two";
        cgi.handle(q, res, srv, cgiLoc);
        expect(res.statusCode == 200 &&
               res.body.find("qs=a=1&b=two") != std::string::npos,
               "cgi: QUERY_STRING exported");

        // 6. non-zero exit -> 502
        cgi.handle(makeReq("GET", "/fail.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 502, "cgi: non-zero exit -> 502");

        // 7. malformed output (no header block) -> 502
        cgi.handle(makeReq("GET", "/malformed.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 502, "cgi: no header block -> 502");

        // 8. hang -> 504 (fast via WEBSERV_CGI_TIMEOUT_MS)
        setenv("WEBSERV_CGI_TIMEOUT_MS", "300", 1);
        cgi.handle(makeReq("GET", "/loop.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 504, "cgi: hang -> 504");
        unsetenv("WEBSERV_CGI_TIMEOUT_MS");

        // 9. non-CGI extension in a CGI location -> 400
        writeFile(cgiDir + "/readme.txt", "not a script");
        cgi.handle(makeReq("GET", "/readme.txt", ""), res, srv, cgiLoc);
        expect(res.statusCode == 400,
               "cgi: non-configured extension -> 400");

        // 10. missing script -> 404
        cgi.handle(makeReq("GET", "/ghost.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 404, "cgi: missing script -> 404");

        // 11. traversal never reaches execve
        writeFile(root + "/evil.py", "print('pwned')\n");
        cgi.handle(makeReq("GET", "/../evil.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 403, "cgi: traversal -> 403");

        // 12. non-POST/GET -> 405 + Allow
        cgi.handle(makeReq("DELETE", "/ok.py", ""), res, srv, cgiLoc);
        expect(res.statusCode == 405 && res.headers["Allow"] == "GET, POST",
               "cgi: DELETE -> 405 Allow GET, POST");

        // 13. no interpreter configured -> 500
        Locations noInt = makeLoc("/cgi-bin");
        noInt.setRoot(cgiDir);
        noInt.addCgiExtension(".py");
        cgi.handle(makeReq("GET", "/ok.py", ""), res, srv, noInt);
        expect(res.statusCode == 500, "cgi: no interpreter -> 500");

        // 14. interpreter that does not exist -> 502 (exit 127)
        Locations badInt = makeLoc("/cgi-bin");
        badInt.setRoot(cgiDir);
        badInt.addCgiExtension(".py");
        badInt.setCgiInterpreter("/nonexistent/interp");
        cgi.handle(makeReq("GET", "/ok.py", ""), res, srv, badInt);
        expect(res.statusCode == 502,
               "cgi: exec failure -> 502 (child exit 127)");
    }

    // ---- cleanup ---------------------------------------------------------
    std::system(cmd.c_str());

    std::cout << "\n" << (g_fail ? "FAILURES: " : "All tests passed. count=")
              << (g_fail ? "" : "") << "\n";
    if (g_fail)
        std::cout << g_fail << " test(s) failed\n";
    return g_fail ? 1 : 0;
}

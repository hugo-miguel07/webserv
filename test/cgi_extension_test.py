#!/usr/bin/env python3
"""Run `make && python3 -B test/cgi_extension_test.py` for extension-based CGI routing."""

import json
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from integration_test import PROJECT, receive_response


def main():
    with tempfile.TemporaryDirectory(prefix="webserv-cgi-extension-") as temporary:
        work = Path(temporary)
        site = work / "site"
        assets = work / "assets"
        own = work / "own"
        site.mkdir()
        own.mkdir()
        (assets / "directory").mkdir(parents=True)
        (site / "index.html").write_bytes(b"home")
        (assets / "directory/youpi.bla").write_bytes(b"static bla contents")
        calls = work / "calls.log"
        for kind in ("generic", "special"):
            executable = work / kind
            executable.write_text(
                "#!/usr/bin/python3\n"
                "import json, os, sys\n"
                "body = sys.stdin.buffer.read()\n"
                "with open(%r, 'a') as calls: calls.write(%r + '\\n')\n" % (str(calls), kind)
                + "print('Content-Type: application/json\\n')\n"
                + "print(json.dumps({'kind': %r, 'method': os.environ['REQUEST_METHOD'], " % kind
                + "'body': body.hex(), 'query': os.environ.get('QUERY_STRING', ''), "
                + "'filename': os.path.normpath(os.environ['SCRIPT_FILENAME']), "
                + "'argument': os.path.normpath(sys.argv[1])}))\n"
            )
            executable.chmod(0o755)
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        config = work / "test.config"
        config.write_text("""
server {
    listen %d;
    server_name localhost;
    root %s;
    index index.html;
    client_max_body_size 1K;
    location / { allow_methods GET; }
    location /directory { root %s; allow_methods GET; client_max_body_size 64; }
    location .special.bla { allow_methods POST; cgi_extension .bla; cgi_path %s/special; }
    location .bla { allow_methods POST; cgi_extension .bla; cgi_path %s/generic; }
    location .limited.bla { allow_methods POST; client_max_body_size 5; cgi_extension .bla; cgi_path %s/generic; }
    location .own.bla { root %s; allow_methods POST; cgi_extension .bla; cgi_path %s/generic; }
    location .blocked.bla { allow_methods GET; cgi_extension .bla; cgi_path %s/generic; }
}
server {
    listen %d;
    server_name extension-only.test;
    root %s;
    client_max_body_size 1K;
    location .bla { allow_methods POST; cgi_extension .bla; cgi_path %s/generic; }
}
""" % (port, site, assets, work, work, work, own, work, work, port, site, work))

        passed = 0

        def check(condition, description):
            nonlocal passed
            assert condition, description
            passed += 1
            print("[PASS]", description, flush=True)

        def count_calls():
            return len(calls.read_text().splitlines()) if calls.exists() else 0

        def connect():
            return socket.create_connection(("127.0.0.1", port), timeout=3)

        def request(method, path, body=b"", host="localhost", chunked=False):
            framing = "Transfer-Encoding: chunked\r\n" if chunked else "Content-Length: %d\r\n" % len(body)
            head = ("%s %s HTTP/1.1\r\nHost: %s\r\n%s\r\n" % (method, path, host, framing)).encode()
            wire = (("%x\r\n" % len(body)).encode() + body + b"\r\n0\r\n\r\n") if chunked else body
            with connect() as client:
                client.sendall(head + wire)
                return receive_response(client)

        def cgi_result(path, body=b"hello", host="localhost", chunked=False):
            status, _, result = request("POST", path, body, host, chunked)
            assert status == 200, (path, status, result)
            result = json.loads(result)
            assert result["method"] == "POST" and result["body"] == body.hex(), result
            return result

        with (work / "server.log").open("w+") as log:
            process = subprocess.Popen([str(PROJECT / "webserv"), str(config)], cwd=PROJECT, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 5
                while True:
                    if process.poll() is not None:
                        raise AssertionError("server stopped during startup")
                    try:
                        with connect():
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(.02)
                before = count_calls()
                status, _, body = request("GET", "/directory/youpi.bla")
                check(status == 200 and body == b"static bla contents" and count_calls() == before,
                      "GET .bla serves static content without running CGI")
                result = cgi_result("/directory/youpi.bla")
                expected = str(assets / "directory/youpi.bla")
                check(result["kind"] == "generic" and result["filename"] == expected and result["argument"] == expected,
                      "POST .bla overrides GET-only prefix and inherits its root")
                for path in ("/directory/youpla.bla", "/directory/deep/missing.bla"):
                    result = cgi_result(path)
                    check(result["filename"] == str(assets / path.lstrip("/")), "virtual CGI URI " + path)
                result = cgi_result("/root.bla", b"")
                check(result["filename"] == str(site / "root.bla"), "root prefix inherits the server root")
                result = cgi_result("/directory/youpi.bla?source=.txt", bytes(range(20)), chunked=True)
                check(result["query"] == "source=.txt", "query string and binary chunked body reach CGI")
                result = cgi_result("/directory/virtual.special.bla")
                check(result["kind"] == "special", "longest matching extension wins")
                result = cgi_result("/directory/virtual.own.bla")
                check(result["filename"] == str(own / "directory/virtual.own.bla"), "extension can override the filesystem root")
                result = cgi_result("/virtual.bla", host="extension-only.test")
                check(result["filename"] == str(site / "virtual.bla"), "extension route works without a prefix location")
                before = count_calls()
                for method, path, expected_status, allowed in (
                    ("GET", "/directory/youpla.bla", 404, None),
                    ("GET", "/virtual.bla", 404, None),
                    ("POST", "/directory/youpi.bla.txt", 405, "GET"),
                    ("POST", "/directory/folder.bla/other", 405, "GET"),
                    ("DELETE", "/directory/youpi.bla", 405, "GET"),
                    ("HEAD", "/directory/youpi.bla", 405, "GET"),
                    ("POST", "/directory/virtual.blocked.bla", 405, "GET"),
                    ("POST", "/../../virtual.bla", 403, None),
                ):
                    host = "extension-only.test" if path == "/virtual.bla" else "localhost"
                    status, headers, _ = request(method, path, host=host)
                    check(status == expected_status and (allowed is None or headers["allow"] == allowed)
                          and count_calls() == before, "%s %s is rejected without executing CGI" % (method, path))
                result = cgi_result("/directory/virtual.bla", b"a" * 64)
                check(result["body"] == (b"a" * 64).hex(), "extension inherits the prefix's 64-byte body limit")
                before = count_calls()
                status, _, _ = request("POST", "/directory/virtual.bla", b"a" * 65)
                check(status == 413 and count_calls() == before, "inherited limit rejects 65 bytes before CGI")
                result = cgi_result("/directory/virtual.limited.bla", b"12345")
                check(result["body"] == b"12345".hex(), "extension's explicit body limit accepts 5 bytes")
                before = count_calls()
                with connect() as client:
                    client.sendall(b"POST /directory/virtual.limited.bla HTTP/1.1\r\nHost: localhost\r\nContent-Length: 6\r\n\r\n")
                    status, _, _ = receive_response(client)
                check(status == 413 and count_calls() == before, "extension's explicit limit rejects before receiving data")
                print("All CGI extension tests passed. count=%d" % passed)
            except Exception:
                log.seek(0)
                print(log.read())
                raise
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()

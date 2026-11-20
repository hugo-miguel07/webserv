#!/usr/bin/env python3
"""Run `make && python3 test/integration_test.py` to exercise real HTTP requests."""

import http.client
import os
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]


def receive_response(client):
    data = bytearray()
    while True:
        block = client.recv(65536)
        if not block:
            break
        data.extend(block)
    head, body = bytes(data).split(b"\r\n\r\n", 1)
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1])
    headers = {}
    for line in lines[1:]:
        key, value = line.split(":", 1)
        headers[key.lower()] = value.strip()
    assert int(headers["content-length"]) == len(body), "response length mismatch"
    assert headers["connection"].lower() == "close", "connection lifetime mismatch"
    return status, headers, body


def main():
    with tempfile.TemporaryDirectory(prefix="webserv-integration-") as temporary:
        work = Path(temporary)
        root = work / "www"
        for directory in (root / "files", root / "cgi-bin", root / "cgi-get", work / "other"):
            directory.mkdir(parents=True)
        (root / "files/private").mkdir()
        (root / "files/private/secret.txt").write_bytes(b"protected")
        (root / "index.html").write_bytes(b"main index")
        (root / "priority").mkdir()
        (root / "priority/fallback.html").write_bytes(b"fallback index")
        (root / "upload.html").write_bytes(b"upload page")
        (work / "other" / "index.html").write_bytes(b"other virtual host")
        large = b"static response\n" * 200000
        (root / "large.txt").write_bytes(large)
        script = (
            "import os, sys\n"
            "body = sys.stdin.buffer.read()\n"
            "print('Content-Type: text/plain')\n"
            "print()\n"
            "print(os.environ['REQUEST_METHOD'] + '|' + os.environ['QUERY_STRING'] + '|' + body.decode())\n"
        )
        (root / "cgi-bin" / "echo.py").write_text(script)
        (root / "cgi-bin" / "fail.py").write_text("import sys\nsys.exit(3)\n")
        marker = work / "executed"
        (root / "cgi-get" / "restricted.py").write_text(
            "open(%r, 'w').write('executed')\n" % str(marker) + script
        )
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        configuration = work / "test.config"
        configuration.write_text("""
server {
    listen %d;
    server_name localhost;
    root %s;
    index index.html;
    client_max_body_size 2K;
    location / { allow_methods GET; }
    location /priority { allow_methods GET; index first.html fallback.html; }
    location /old { return 302 /new; }
    location /upload { allow_methods POST; upload_store %s/files; }
    location /files { allow_methods GET DELETE; autoindex on; }
    location /files/private { allow_methods GET; }
    location /cgi-bin { allow_methods GET POST; cgi_extension .py; cgi_path /usr/bin/python3; }
    location /cgi-get { allow_methods GET; cgi_extension .py; cgi_path /usr/bin/python3; }
}
server {
    listen %d;
    server_name other.test;
    root %s/other;
    index index.html;
    client_max_body_size 2K;
    location / { allow_methods GET; }
}
server {
    listen %d;
    server_name no-root.test;
    root %s;
    client_max_body_size 2K;
    location /only { allow_methods GET; }
}
""" % (port, root, root, port, work, port, root))

        def connect():
            return socket.create_connection(("127.0.0.1", port), timeout=3)

        def request(method, path, body=b"", headers=(), host="localhost"):
            head = "%s %s HTTP/1.1\r\nHost: %s\r\n" % (method, path, host)
            for key, value in headers:
                head += "%s: %s\r\n" % (key, value)
            names = {key.lower() for key, _ in headers}
            if body and not names.intersection(("content-length", "transfer-encoding")):
                head += "Content-Length: %d\r\n" % len(body)
            with connect() as client:
                client.sendall(head.encode() + b"\r\n" + body)
                return receive_response(client)

        passed = 0

        def check(condition, name):
            nonlocal passed
            assert condition, name
            passed += 1
            print("[PASS]", name)

        with (work / "server.log").open("w+") as log:
            process = subprocess.Popen(
                [str(PROJECT / "webserv"), str(configuration)],
                cwd=PROJECT, stdout=log, stderr=log,
            )
            try:
                for _ in range(100):
                    if process.poll() is not None:
                        raise RuntimeError("server exited during startup")
                    try:
                        with connect():
                            break
                    except ConnectionRefusedError:
                        time.sleep(0.02)
                else:
                    raise RuntimeError("server did not start")

                status, _, body = request("GET", "/")
                check(status == 200 and body == b"main index", "static index through handleRead")
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
                try:
                    connection.request("HEAD", "/", headers={"Host": "localhost"})
                    response = connection.getresponse()
                    check(response.status == 405 and response.getheader("Allow") == "GET",
                          "HEAD is recognized and rejected by the GET-only route with 405")
                finally:
                    connection.close()
                status, _, body = request("GET", "/", host="other.test:%d" % port)
                check(status == 200 and body == b"other virtual host", "virtual host selects its own configuration")
                index = root / "index.html"
                metadata = index.stat()
                index.write_bytes(b"next index")
                os.utime(index, ns=(metadata.st_atime_ns, metadata.st_mtime_ns))
                status, _, body = request("GET", "/")
                check(status == 200 and body == b"next index",
                      "cached index notices a same-size rewrite with preserved mtime")
                replacement = root / "replacement.html"
                replacement.write_bytes(b"swap index")
                os.utime(replacement, ns=(metadata.st_atime_ns, metadata.st_mtime_ns))
                replacement.replace(index)
                status, _, body = request("GET", "/")
                check(status == 200 and body == b"swap index", "cached index notices atomic replacement")
                index.unlink()
                status, _, _ = request("GET", "/")
                check(status == 404, "deleted cached index is no longer served")
                index.write_bytes(b"main index")
                status, _, body = request("GET", "/")
                check(status == 200 and body == b"main index", "recreated index is served immediately")
                status, _, body = request("GET", "/priority/")
                check(status == 200 and body == b"fallback index", "fallback index can be cached")
                (root / "priority/first.html").write_bytes(b"first index")
                status, _, body = request("GET", "/priority/")
                check(status == 200 and body == b"first index", "new higher-priority index overrides cached fallback")
                (root / "priority/first.html").unlink()
                status, _, body = request("GET", "/priority/")
                check(status == 200 and body == b"fallback index", "removed preferred index restores fallback")
                status, _, body = request("GET", "/large.txt")
                check(status == 200 and body == large, "large response is sent completely")
                status, headers, _ = request("GET", "/old")
                check(status == 302 and headers["location"].endswith("/new"), "redirect without an allow_methods list")
                status, headers, _ = request("GET", "/upload")
                check(status == 405 and headers["allow"] == "POST", "specific location takes precedence over /")
                status, _, body = request("GET", "/upload.html")
                check(status == 200 and body == b"upload page", "upload location does not capture the upload HTML page")
                status, _, _ = request("POST", "/upload/raw.txt", b"raw upload")
                check(status == 201 and (root / "files/raw.txt").read_bytes() == b"raw upload", "raw upload reaches UploadHandler")
                status, _, _ = request("POST", "/upload/empty.txt", headers=(("Content-Length", "0"),))
                check(status == 201 and (root / "files/empty.txt").read_bytes() == b"", "empty upload receives a response")
                multipart = (
                    b'--BOUND\r\nContent-Disposition: form-data; name="file"; filename="multi.txt"\r\n'
                    b'Content-Type: text/plain\r\n\r\nContent-Length in file contents\r\n--BOUND--\r\n'
                )
                status, _, _ = request("POST", "/upload", multipart,
                                       (("Content-Type", "multipart/form-data; boundary=BOUND"),))
                check(status == 201 and (root / "files/multi.txt").read_bytes() == b"Content-Length in file contents", "multipart file contents do not change HTTP framing")
                status, _, _ = request("POST", "/upload/chunk.txt", b"5\r\nhello\r\n0\r\n\r\n",
                                       (("Transfer-Encoding", "chunked"),))
                check(status == 201 and (root / "files/chunk.txt").read_bytes() == b"hello", "chunked body is decoded before dispatch")
                status, headers, _ = request("GET", "/files?page=2")
                check(status == 301 and headers["location"] == "/files/?page=2", "directory redirect preserves query")
                status, _, body = request("GET", "/files/")
                check(status == 200 and b"raw.txt" in body, "StaticHandler delegates autoindex to listing handler")
                status, _, body = request("GET", "/files/multi.txt")
                check(status == 200 and body == b"Content-Length in file contents", "uploaded file can be served")
                status, _, _ = request("DELETE", "/files/raw.txt")
                check(status == 204 and not (root / "files/raw.txt").exists(), "DELETE reaches specific location and handler")
                status, headers, _ = request("DELETE", "/index.html")
                check(status == 405 and headers["allow"] == "GET" and (root / "index.html").exists(), "DELETE respects location policy")
                status, headers, _ = request("DELETE", "/files/private/secret.txt")
                check(status == 405 and headers["allow"] == "GET" and (root / "files/private/secret.txt").exists(), "nested location policy takes precedence over its parent")
                status, _, body = request("GET", "/cgi-bin/echo.py?a=1")
                check(status == 200 and body == b"GET|a=1|\n", "CGI receives method and query")
                status, _, body = request("POST", "/cgi-bin/echo.py", b"cgi body",
                                          (("Content-Type", "text/plain"),))
                check(status == 200 and body == b"POST||cgi body\n", "CGI receives decoded request body")
                status, _, _ = request("GET", "/cgi-bin/missing.py")
                check(status == 404, "missing CGI response is preserved")
                status, _, _ = request("GET", "/cgi-bin/fail.py")
                check(status == 502, "CGI failure response is sent without stopping the server")
                status, _, _ = request("POST", "/cgi-get/restricted.py", b"forbidden")
                check(status == 405 and not marker.exists(), "method policy is checked before executing CGI")
                with connect() as client:
                    client.sendall(b"GET / HTTP/1.1\r\nHo")
                    client.settimeout(0.15)
                    try:
                        client.recv(1)
                    except socket.timeout:
                        pass
                    else:
                        raise AssertionError("incomplete headers dispatched early")
                    check(process.poll() is None, "fragmented headers wait without stopping the server")
                    client.settimeout(3)
                    client.sendall(b"st: localhost\r\n\r\n")
                    status, _, body = receive_response(client)
                check(status == 200 and body == b"main index", "completed fragmented headers reach the static handler")
                with connect() as client:
                    client.sendall(b"POST /upload/fragment.txt HTTP/1.1\r\nHost: localhost\r\ncontent-length:5\r\n\r\nhe")
                    client.settimeout(0.15)
                    try:
                        client.recv(1)
                    except socket.timeout:
                        pass
                    else:
                        raise AssertionError("incomplete body dispatched early")
                    check(not (root / "files/fragment.txt").exists(), "fragmented request waits for the complete body")
                    client.settimeout(3)
                    client.sendall(b"llo")
                    status, _, _ = receive_response(client)
                check(status == 201 and (root / "files/fragment.txt").read_bytes() == b"hello", "lowercase header and no space after colon are accepted")
                status, _, _ = request("POST", "/upload", headers=(("Content-Length", "2001"),))
                check(status == 413, "oversized declared body rejected before waiting for bytes")
                status, _, _ = request("POST", "/upload/large.txt", b"7d1\r\n" + b"a" * 2001 + b"\r\n0\r\n\r\n",
                                       (("Transfer-Encoding", "chunked"),))
                check(status == 413 and not (root / "files/large.txt").exists(), "oversized chunked body never reaches the handler")
                status, _, _ = request("POST", "/upload", headers=(("Content-Length", "abc"),))
                check(status == 400, "invalid framing becomes an HTTP error")
                status, _, _ = request("POST", "/upload", headers=(("Content-Length", "0"), ("Transfer-Encoding", "chunked")))
                check(status == 400, "ambiguous framing is rejected")
                status, _, _ = request("POST", "/upload", headers=(("Content-Length", "0"), ("content-length", "1")))
                check(status == 400, "duplicate framing headers are rejected")
                status, _, _ = request("GET", "/", host="no-root.test")
                check(status == 404, "missing location error from Request becomes HTTP 404")
                status, _, _ = request("GET", "/missing")
                check(status == 404 and process.poll() is None, "server continues serving after malformed requests")
                print("All integration tests passed. count=%d" % passed)
            except Exception:
                log.flush()
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

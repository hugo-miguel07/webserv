#!/usr/bin/env python3
"""Run `make && python3 -B test/body_limit_test.py` for per-location body limits."""

import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from integration_test import PROJECT, receive_response


def main():
    with tempfile.TemporaryDirectory(prefix="webserv-body-limit-") as temporary:
        work = Path(temporary)
        uploads = work / "uploads"
        uploads.mkdir()
        (work / "index.html").write_bytes(b"alive")
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
    location /post_body {
        allow_methods POST;
        client_max_body_size 100;
        upload_store %s;
    }
    location /larger {
        allow_methods POST;
        client_max_body_size 2K;
        upload_store %s;
    }
    location /inherit { allow_methods POST; upload_store %s; }
    location /post_body/nested {
        allow_methods POST;
        client_max_body_size 50;
        upload_store %s;
    }
}
server {
    listen %d;
    server_name other.test;
    client_max_body_size 2K;
    location /inherit { allow_methods POST; upload_store %s; }
}
""" % (port, work, uploads, uploads, uploads, uploads, port, uploads))

        passed = 0

        def check(condition, description):
            nonlocal passed
            assert condition, description
            passed += 1
            print("[PASS]", description, flush=True)

        def connect():
            return socket.create_connection(("127.0.0.1", port), timeout=3)

        def head(path, headers, host="localhost", method="POST"):
            value = "%s %s HTTP/1.1\r\nHost: %s\r\n" % (method, path, host)
            value += "".join("%s: %s\r\n" % pair for pair in headers)
            return value.encode() + b"\r\n"

        def request(path, body=b"", chunked=False, host="localhost"):
            if chunked:
                headers = (("Transfer-Encoding", "chunked"),)
                wire = ("%x\r\n" % len(body)).encode() + body + b"\r\n0\r\n\r\n" if body else b"0\r\n\r\n"
            else:
                headers = (("Content-Length", str(len(body))),)
                wire = body
            with connect() as client:
                client.sendall(head(path, headers, host) + wire)
                return receive_response(client)[0]

        def pending(client):
            client.settimeout(.1)
            try:
                client.recv(1)
            except socket.timeout:
                pass
            else:
                raise AssertionError("incomplete body received a response")
            finally:
                client.settimeout(3)

        with (work / "server.log").open("w+") as log:
            process = subprocess.Popen([str(PROJECT / "webserv"), str(config)],
                                       cwd=PROJECT, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 5
                while True:
                    if process.poll() is not None:
                        raise AssertionError("server exited during startup")
                    try:
                        with connect():
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(.02)

                for chunked in (False, True):
                    payload = b"x" * 262144
                    headers = (("Transfer-Encoding", "chunked"),) if chunked else (("Content-Length", str(len(payload))),)
                    wire = b"40000\r\n" + payload + b"\r\n0\r\n\r\n" if chunked else payload
                    with connect() as client:
                        client.sendall(head("/post_body", headers) + wire)
                        check(receive_response(client)[0] == 413,
                              "queued %s payload does not reset the 413 response" % (
                                  "chunked" if chunked else "fixed"))
                for chunked in (False, True):
                    for size, expected in ((0, 201), (100, 201), (101, 413)):
                        before = (uploads / "post_body").read_bytes() if (uploads / "post_body").exists() else None
                        payload = bytes(range(size))
                        status = request("/post_body", payload, chunked)
                        saved = (uploads / "post_body").read_bytes()
                        check(status == expected and saved == (payload if expected == 201 else before),
                              "%s body: %d bytes, HTTP %d, file contents preserved" % (
                                  "chunked" if chunked else "fixed", size, expected))

                with connect() as client:
                    client.sendall(head("/post_body", (("Content-Length", "101"),)))
                    check(receive_response(client)[0] == 413, "oversized Content-Length rejected without body")
                with connect() as client:
                    client.sendall(head("/post_body", (("Transfer-Encoding", "chunked"),)) + b"65\r\n")
                    check(receive_response(client)[0] == 413, "oversized chunk rejected without data")
                for chunked in (False, True):
                    with connect() as client:
                        headers = (("Transfer-Encoding", "chunked"),) if chunked else (("Content-Length", "101"),)
                        client.sendall(head("/post_body", headers) + (b"65\r\n" if chunked else b""))
                        time.sleep(.05)
                        client.sendall(b"x" * 101 + (b"\r\n0\r\n\r\n" if chunked else b""))
                        check(receive_response(client)[0] == 413,
                              "late %s payload does not reset the 413 response" % (
                                  "chunked" if chunked else "fixed"))
                first = bytes(range(60))
                last = bytes(range(60, 100))
                for second_size, tail, expected in ((b"28", last + b"\r\n0\r\nX-End: yes\r\n\r\n", 201),
                                                     (b"29", b"", 413)):
                    before = (uploads / "post_body").read_bytes()
                    with connect() as client:
                        client.sendall(head("/post_body", (("Transfer-Encoding", "chunked"),)) + b"3")
                        pending(client)
                        client.sendall(b"c;extension=yes\r\n" + first + b"\r\n")
                        pending(client)
                        client.sendall(second_size + b"\r\n" + tail)
                        status = receive_response(client)[0]
                    saved = (uploads / "post_body").read_bytes()
                    check(status == expected and saved == (first + last if expected == 201 else before),
                          "fragmented cumulative chunks: HTTP %d, framing excluded from limit" % expected)

                with connect() as client:
                    client.sendall(head("/post_body", (("Content-Length", "100"),)) + first)
                    pending(client)
                    client.sendall(last)
                    check(receive_response(client)[0] == 201 and (uploads / "post_body").read_bytes() == first + last,
                          "fixed body at limit can arrive in fragments")

                for path, size, expected in (("/inherit", 1000, 201), ("/inherit", 1001, 413),
                                             ("/larger", 1500, 201), ("/larger", 2000, 201),
                                             ("/larger", 2001, 413), ("/post_body/nested", 50, 201),
                                             ("/post_body/nested", 51, 413), ("/post_body?x=1", 100, 201),
                                             ("/post_body?x=1", 101, 413), ("/post_body_more", 101, 405)):
                    check(request(path, b"a" * size) == expected,
                          "%s with %d bytes -> %d" % (path, size, expected))
                for path, size, expected in (("/inherit", 1001, 413), ("/larger", 1500, 201),
                                             ("/larger", 2001, 413), ("/post_body/nested", 51, 413)):
                    check(request(path, b"a" * size, chunked=True) == expected,
                          "chunked %s with %d bytes -> %d" % (path, size, expected))
                check(request("/inherit", b"x" * 1500, host="other.test") == 201
                      and request("/inherit", b"x" * 1500) == 413,
                      "server limit belongs to the selected virtual host")
                with connect() as client:
                    client.sendall(head("/", (), method="GET"))
                    status, _, body = receive_response(client)
                    check(status == 200 and body == b"alive", "server stays responsive after rejected bodies")
                descriptors = Path("/proc") / str(process.pid) / "fd"
                if descriptors.is_dir():
                    def socket_descriptors():
                        result = set()
                        for descriptor in descriptors.iterdir():
                            try:
                                target = str(descriptor.readlink())
                                if target.startswith("socket:["):
                                    result.add(target)
                            except FileNotFoundError:
                                pass
                        return result

                    before = socket_descriptors()
                    with connect() as client:
                        client.sendall(head("/post_body", (("Content-Length", "101"),)))
                        status = receive_response(client)[0]
                        rejected = socket_descriptors() - before
                        deadline = time.monotonic() + 4
                        while rejected & socket_descriptors() and time.monotonic() < deadline:
                            time.sleep(.05)
                        check(status == 413 and len(rejected) == 1
                              and not rejected & socket_descriptors(),
                              "rejected idle connections are released without waiting for the client to close")
                print("All body limit tests passed. count=%d" % passed)
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

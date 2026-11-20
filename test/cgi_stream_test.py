#!/usr/bin/env python3
"""Regression tests for CGI environment and simultaneous stdin/stdout traffic.

Run `make && python3 -B test/cgi_stream_test.py`.
"""

import http.client
import json
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from integration_test import PROJECT


def main():
    with tempfile.TemporaryDirectory(prefix="webserv-cgi-stream-") as temporary:
        work = Path(temporary)
        env_cgi = work / "environment"
        env_cgi.write_text(
            "#!/usr/bin/python3\n"
            "import json, os, sys\n"
            "body = sys.stdin.buffer.read()\n"
            "print('Content-Type: application/json\\n')\n"
            "print(json.dumps({'env': dict(os.environ), 'body': body.hex()}))\n"
        )
        stream_cgi = work / "stream"
        stream_cgi.write_text(
            "#!/usr/bin/python3\n"
            "import sys\n"
            "output = sys.stdout.buffer\n"
            "output.write(b'Content-Type: application/octet-stream\\r\\n\\r\\n')\n"
            "output.write(b'prefix' * 65536)\n"
            "output.flush()\n"
            "while True:\n"
            "    block = sys.stdin.buffer.read(65536)\n"
            "    if not block: break\n"
            "    output.write(block)\n"
            "    output.flush()\n"
        )
        for executable in (env_cgi, stream_cgi):
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
    client_max_body_size 20M;
    location .env { allow_methods POST; cgi_extension .env; cgi_path %s; }
    location .stream { allow_methods POST; cgi_extension .stream; cgi_path %s; }
}
""" % (port, work, env_cgi, stream_cgi))

        def request(path, body=b"", headers=None, chunked=False):
            connection = http.client.HTTPConnection("localhost", port, timeout=15)
            try:
                connection.request("POST", path, body=body, headers=headers or {},
                                   encode_chunked=chunked)
                response = connection.getresponse()
                result = response.read()
                assert response.status == 200, (response.status, result[:1000])
                assert int(response.getheader("Content-Length")) == len(result)
                return result
            finally:
                connection.close()

        with (work / "server.log").open("w+") as log:
            server = subprocess.Popen([str(PROJECT / "webserv"), str(config)],
                                      cwd=PROJECT, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 5
                while True:
                    if server.poll() is not None:
                        raise RuntimeError("server stopped before accepting requests")
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=.1):
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(.02)

                passed = 0

                def check(condition, description):
                    nonlocal passed
                    assert condition, description
                    passed += 1
                    print("[PASS]", description, flush=True)

                body = b"a\x00b\r\nc"
                result = json.loads(request(
                    "/virtual.env?key=value", body,
                    {"X-SeCrEt-HeAdEr-FoR-TeSt": "7",
                     "Content-Type": "application/octet-stream"}))
                env = result["env"]
                check(env["PATH_INFO"] == "/virtual.env" and env["SCRIPT_NAME"] == "",
                      "CGI receives the URI once through PATH_INFO")
                check(env["SERVER_PORT"] == str(port) and env["SERVER_PROTOCOL"] == "HTTP/1.1",
                      "CGI receives the actual listening port and protocol")
                check(env["HTTP_X_SECRET_HEADER_FOR_TEST"] == "7",
                      "custom HTTP headers reach CGI with normalized names")
                check(env["CONTENT_LENGTH"] == str(len(body))
                      and env["CONTENT_TYPE"] == "application/octet-stream"
                      and result["body"] == body.hex(),
                      "CGI content metadata and binary input are preserved")
                check(env["QUERY_STRING"] == "key=value"
                      and env["REQUEST_METHOD"] == "POST",
                      "CGI receives the query and method separately from the path")

                result = json.loads(request("/empty.env"))
                check(result["env"]["CONTENT_LENGTH"] == "0" and result["body"] == "",
                      "empty CGI requests have a zero content length and EOF on stdin")

                pieces = [b"a\x00b", b"\r\nc"]
                result = json.loads(request("/chunked.env", iter(pieces), chunked=True))
                check(result["env"]["CONTENT_LENGTH"] == "6"
                      and "HTTP_TRANSFER_ENCODING" not in result["env"]
                      and result["body"] == b"".join(pieces).hex(),
                      "chunked input is decoded before CGI metadata is built")

                body = bytes(range(256)) * (12 * 1024 * 1024 // 256)
                result = request("/virtual.stream", body)
                check(result == b"prefix" * 65536 + body,
                      "CGI can write before reading and return more than 10 MiB intact")
                result = json.loads(request("/after-stream.env"))
                check(result["env"]["PATH_INFO"] == "/after-stream.env",
                      "server accepts requests after completing the large duplex exchange")
                print("All CGI stream tests passed. count=%d" % passed, flush=True)
            finally:
                if server.poll() is None:
                    server.send_signal(signal.SIGINT)
                    try:
                        server.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()


if __name__ == "__main__":
    main()

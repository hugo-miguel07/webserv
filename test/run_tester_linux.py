#!/usr/bin/env python3
"""Run the supplied tester with its runtime files on the Linux filesystem."""

import argparse
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def run():
    required = ("tester", "cgi_tester", "www", "YoupiBanane",
                "configurations/tester.config")
    for name in required:
        if not (PROJECT / name).exists():
            raise RuntimeError("Falta o ficheiro ou diretório: " + str(PROJECT / name))
    subprocess.run(["make", "-j4"], cwd=PROJECT, check=True)

    # Force /tmp: TMPDIR might itself point to the Windows filesystem.
    with tempfile.TemporaryDirectory(prefix="webserv-tester-", dir="/tmp") as temporary:
        work = Path(temporary)
        for name in ("webserv", "tester", "cgi_tester"):
            shutil.copy2(PROJECT / name, work / name)
        for name in ("www", "YoupiBanane"):
            shutil.copytree(PROJECT / name, work / name, symlinks=True)
        (work / "directory").symlink_to("YoupiBanane", target_is_directory=True)

        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        config, replacements = re.subn(
            r"(?m)^(\s*listen\s+)\d+(\s*;)",
            lambda match: match.group(1) + str(port) + match.group(2),
            (PROJECT / "configurations/tester.config").read_text(), count=1)
        if replacements != 1:
            raise RuntimeError("Não foi encontrada a diretiva listen no tester.config")
        (work / "tester.config").write_text(config)
        url = "http://localhost:%d" % port
        print("Ficheiros de teste: %s\nServidor temporário: %s" % (work, url), flush=True)
        server = None
        tester = None
        with (work / "server.log").open("w+") as server_log, \
                (work / "tester.log").open("w+") as tester_log:
            try:
                server = subprocess.Popen(
                    [str(work / "webserv"), str(work / "tester.config")],
                    cwd=work, stdout=server_log, stderr=server_log,
                    start_new_session=True)
                deadline = time.monotonic() + 5
                while True:
                    if server.poll() is not None:
                        raise RuntimeError("O servidor temporário não arrancou")
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=.1):
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise RuntimeError("O servidor não aceitou ligações em 5 segundos")
                        time.sleep(.02)

                tester = subprocess.Popen(
                    [str(work / "tester"), url], cwd=work, stdin=subprocess.PIPE,
                    stdout=tester_log, stderr=tester_log, start_new_session=True)
                tester.stdin.write(b"\n" * 20)
                tester.stdin.close()
                started = time.monotonic()
                last_update = started
                with (work / "tester.log").open(errors="replace") as output:
                    while True:
                        finished = tester.poll() is not None
                        text = output.read()
                        if text:
                            print(text, end="", flush=True)
                        if finished:
                            break
                        if time.monotonic() - last_update >= 30:
                            print("\n[tester em execução há %.0f s]" %
                                  (time.monotonic() - started), flush=True)
                            last_update = time.monotonic()
                        time.sleep(.5)
                print("\nTester terminou com código %d." % tester.returncode, flush=True)
                return tester.returncode
            finally:
                stop(tester)
                if server is not None and server.poll() is None:
                    server.send_signal(signal.SIGINT)
                    try:
                        server.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        stop(server)
                if server is not None and server.returncode not in (0, 255):
                    server_log.seek(0)
                    print(server_log.read()[-4000:], file=sys.stderr)


def main():
    argparse.ArgumentParser(description=(
        "Compila o webserv e corre o tester numa pasta temporária em /tmp, "
        "com uma porta livre. Pode ser interrompido com Ctrl+C."
    )).parse_args()
    try:
        return run()
    except KeyboardInterrupt:
        print("\nTeste interrompido; processos temporários terminados.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

"""Run the same native host directly and behind a real local TLS nginx edge."""
from pathlib import Path
import contextlib
import http.client
import os
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

from http_cli import running_host


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


@contextlib.contextmanager
def edge(nginx, root, fragment, port):
    root.mkdir()
    config = root / "nginx.conf"
    config.write_text("worker_processes 1;\npid nginx.pid;\nerror_log error.log warn;\n"
                      "events { worker_connections 128; }\nhttp { access_log off;\n" + fragment + "\n}\n")
    command = [str(nginx), "-p", str(root) + os.sep, "-c", str(config)]
    subprocess.run(command + ["-t"], capture_output=True, text=True, check=True, timeout=15)
    with (root / "process.log").open("w") as output:
        process = subprocess.Popen(command + ["-g", "daemon off;"], stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 15
            while True:
                if process.poll() is not None:
                    raise AssertionError(("nginx startup", (root / "process.log").read_text()))
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                        break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.02)
            yield process
        finally:
            if process.poll() is None:
                process.send_signal(getattr(signal, "SIGQUIT", signal.SIGTERM))
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def request(context, port, method, path, body=None, chunked=False):
    connection = http.client.HTTPSConnection("127.0.0.1", port, context=context, timeout=10)
    try:
        connection.request(method, path, body, headers={"Host": "app.example.invalid"}, encode_chunked=chunked)
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def check(nginx, executable, root, openssl, curl):
    cert, key = root / "certificate.pem", root / "private.key"
    subprocess.run([str(openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                    "-subj", "/CN=app.example.invalid", "-addext", "subjectAltName=DNS:app.example.invalid,IP:127.0.0.1",
                    "-keyout", str(key), "-out", str(cert)], capture_output=True, check=True, timeout=30)
    key.chmod(0o600)
    static = root / "public"
    static.mkdir()
    (static / "hello.txt").write_bytes(b"nginx static fixture")
    context = ssl.create_default_context(cafile=str(cert))
    template = (Path(__file__).resolve().parents[1] / "deploy/nginx/iiwebserver.conf").read_text()
    with running_host(executable, root) as (host, backend):
        direct = http.client.HTTPConnection("127.0.0.1", backend, timeout=10)
        direct.request("GET", "/health")
        response = direct.getresponse()
        assert response.status == 200 and response.read() == b'{"status":"ok"}'
        direct.close()

        def configuration(port):
            return (template.replace("127.0.0.1:8080", "127.0.0.1:" + str(backend))
                    .replace("listen 443 ssl;", "listen 127.0.0.1:" + str(port) + " ssl;")
                    .replace("/etc/iiwebserver/fullchain.pem", cert.as_posix())
                    .replace("/etc/iiwebserver/privkey.pem", key.as_posix())
                    .replace("/srv/iiwebserver/public/", static.as_posix() + "/"))

        port = free_port()
        with edge(nginx, root / "edge", configuration(port), port):
            assert request(context, port, "GET", "/health") == (200, b'{"status":"ok"}')
            payload = b"one source, local and proxy\0\xff"
            assert request(context, port, "POST", "/api/echo", payload) == (200, payload)
            assert request(context, port, "POST", "/api/echo", [b"chunk", b"ed\0body"], True) == (200, b"chunked\0body")
            assert request(context, port, "GET", "/static/hello.txt") == (200, b"nginx static fixture")
            connection = http.client.HTTPSConnection("127.0.0.1", port, context=context, timeout=10)
            connection.putrequest("POST", "/api/echo")
            connection.putheader("Content-Length", str(2 * 1024 * 1024 + 1))
            connection.endheaders()
            response = connection.getresponse()
            assert response.status == 413
            response.read()
            connection.close()
            body = root / "h2.json"
            version = subprocess.run([str(curl), "--noproxy", "*", "--silent", "--show-error", "--fail",
                                      "--http2", "--max-time", "10", "--cacert", str(cert), "--resolve",
                                      "app.example.invalid:" + str(port) + ":127.0.0.1", "--output", str(body),
                                      "--write-out", "%{http_version}", "https://app.example.invalid:" + str(port) + "/health"],
                                     capture_output=True, text=True, check=True, timeout=15)
            assert version.stdout.strip() == "2" and body.read_bytes() == b'{"status":"ok"}', version.stdout
            assert host.poll() is None
        port = free_port()
        limited = configuration(port).replace("rate=20r/s", "rate=1r/s").replace("burst=40", "burst=1")
        with edge(nginx, root / "limited", limited, port):
            statuses = [request(context, port, "GET", "/health")[0] for _ in range(5)]
            assert 200 in statuses and 429 in statuses, statuses
    print("same native executable: direct HTTP, nginx TLS/HTTP2, buffered chunked upload, static files, body and rate limits passed")


if __name__ == "__main__":
    nginx, executable, build, openssl, curl = map(lambda value: Path(value).resolve(), sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="nginx-http-", dir=build) as temporary:
        check(nginx, executable, Path(temporary), openssl, curl)

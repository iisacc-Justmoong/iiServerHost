"""Exercise the reference host over real sockets and terminate only this child."""
from pathlib import Path
import contextlib
import http.client
import os
import queue
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time


@contextlib.contextmanager
def running_host(executable, directory, *options):
    env = os.environ.copy()
    for key in ("DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH"):
        env.pop(key, None)
    with (directory / "host.log").open("w") as log:
        process = subprocess.Popen([str(executable), "--port", "0", *options],
                                   stdout=subprocess.PIPE, stderr=log, text=True, env=env)
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=30).strip()
            if not line.startswith("http://127.0.0.1:"):
                raise AssertionError(("host startup", line, process.poll()))
            port = int(line.rsplit(":", 1)[1])
            yield process, port
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            process.stdout.close()


def check(executable, directory):
    with running_host(executable, directory, "--grace-ms", "1000", "--max-body-bytes", "1024") as (process, port):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        connection.request("GET", "/health")
        response = connection.getresponse()
        assert response.status == 200 and response.read() == b'{"status":"ok"}'
        connection.close()
        payload = b"binary\0body\xff"
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        connection.request("POST", "/api/echo", payload)
        response = connection.getresponse()
        assert response.status == 200 and response.read() == payload
        connection.close()
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        connection.putrequest("POST", "/api/echo")
        connection.putheader("Content-Length", "1025")
        connection.endheaders()
        response = connection.getresponse()
        assert response.status == 413
        response.read()
        connection.close()
        # The process must preserve an admitted partial request on SIGTERM.
        with socket.create_connection(("127.0.0.1", port), timeout=5) as active:
            active.sendall(b"POST /api/echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nab")
            time.sleep(0.1)
            process.send_signal(signal.SIGTERM)
            time.sleep(0.1)
            active.sendall(b"cd")
            reply = bytearray()
            while True:
                part = active.recv(4096)
                if not part:
                    break
                reply.extend(part)
            assert b"200 Response" in reply and reply.endswith(b"abcd"), reply
        assert process.wait(timeout=5) == 0
    print("native HTTP host, body bound and SIGTERM drain passed")


if __name__ == "__main__":
    executable, build = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="http-cli-", dir=build) as temporary:
        check(executable, Path(temporary))

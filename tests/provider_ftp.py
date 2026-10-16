"""Real TLS FTP file creation without requiring the optional rclone backend."""
import ftplib
import io
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time

interpreter = os.environ.get("IISERVERHOST_TEST_FTP_PYTHON")
if interpreter and not os.environ.get("IISERVERHOST_FTP_TEST_CHILD"):
    environment = dict(os.environ, IISERVERHOST_FTP_TEST_CHILD="1")
    raise SystemExit(subprocess.call([interpreter, "-B", __file__, *sys.argv[1:]], env=environment))
try:
    import OpenSSL
    import pyftpdlib
except ImportError:
    print("SKIP: the optional FTP TLS interpreter dependencies are unavailable")
    raise SystemExit(77)

build = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="provider-ftp-", dir=build) as temporary:
    root = Path(temporary)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    environment = dict(os.environ, IISERVERHOST_FTP_USER="fixture", IISERVERHOST_FTP_PASSWORD="fixture-secret")
    # Exercise discovery of the adjacent build runtime, not a developer SDK.
    environment = {key: value for key, value in environment.items()
                   if not key.startswith(("IIFILEPROVIDER_", "DYLD_"))}
    command = [sys.executable, "-B", str(build / "ftp_server.py"), "--root", str(root),
               "--port", str(port), "--certificate", str(build / "test-cert.pem"),
               "--key", str(build / "test-key.pem")]
    with (root / "server.log").open("w") as log:
        process = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 30
            while True:
                assert process.poll() is None, (root / "server.log").read_text()
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    assert time.monotonic() < deadline, "FTP startup timeout"
                    time.sleep(0.02)
            client = ftplib.FTP_TLS(context=ssl.create_default_context(cafile=str(build / "test-ca.pem")))
            client.connect("127.0.0.1", port, timeout=10)
            client.login("fixture", "fixture-secret")
            client.prot_p()
            try:
                client.mkd("owned")
                client.storbinary("STOR owned/생성.bin", io.BytesIO(b"first"))
                client.storbinary("APPE owned/생성.bin", io.BytesIO(b"-append"))
                client.rename("owned/생성.bin", "owned/renamed.bin")
                received = bytearray()
                client.retrbinary("RETR owned/renamed.bin", received.extend)
                assert received == b"first-append"
                client.storbinary("STOU owned/unique", io.BytesIO(b"exclusive"))
                entries = list((root / "owned").iterdir())
                assert len(entries) == 2 and any(path.read_bytes() == b"exclusive" for path in entries)
                assert (root / "owned/renamed.bin").read_bytes() == b"first-append"
            finally:
                client.close()
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
print("TLS FTP provider MKD/STOR/APPE/rename/STOU and Unicode payload checks passed")

"""Real local protocol interoperability; no accounts, NAS mounts, or public listeners required."""
import base64
import asyncio
import contextlib
import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

transfer_exe, storage_exe, build_dir = map(lambda x: Path(x).resolve(), sys.argv[1:4])
rclone = os.environ.get("IISERVERHOST_TEST_RCLONE", "")
if not rclone:
    print("SKIP: set IISERVERHOST_TEST_RCLONE to exercise real protocol servers")
    sys.exit(77)
capabilities = json.loads(subprocess.check_output([transfer_exe, "--capabilities"], text=True))
if not capabilities["protocols"]:
    print("SKIP: libcurl backend disabled")
    sys.exit(77)

payload = bytes(range(256)) * 8192 + b"\0\xffarbitrary-extension\0"
digest = hashlib.sha256(payload).hexdigest()
username, password = "account-fixture", "fixture-service-password"
checks = []


def private_json(path, data):
    path.write_text(json.dumps(data), encoding="utf-8")
    path.chmod(0o600)
    return str(path)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


with tempfile.TemporaryDirectory(prefix="protocol-process-", dir=build_dir) as temporary:
    base = Path(temporary)
    runtime = base / "runtime"
    runtime.mkdir(mode=0o700)
    root = base / "files"
    root.mkdir()
    source = base / "모델 source.bin"
    source.write_bytes(payload)
    output = base / "download.bin"
    backend = {"executable": rclone, "runtimeDirectory": str(runtime),
               "ftpPythonExecutable": os.environ.get("IISERVERHOST_TEST_FTP_PYTHON", sys.executable),
               "ftpServerScript": str(build_dir / "ftp_server.py")}

    def transfer(operation, url, path, *, success=True, **kwargs):
        request = dict(operation=operation, url=url, localPath=str(path), timeoutMs=15000,
                       connectTimeoutMs=3000, **kwargs)
        completed = subprocess.run([transfer_exe, "--request", private_json(base / "request.json", request)],
                                   capture_output=True, text=True, timeout=20)
        result = json.loads(completed.stdout)
        assert result["ok"] == success, (operation, url, result, completed.stderr)
        assert (completed.returncode == 0) == success, result
        return result

    def storage(operation, *, remote_config=None, success=True, **kwargs):
        b = dict(backend)
        if remote_config:
            b["configFile"] = str(remote_config)
        config = {"backend": b, "request": dict(operation=operation, timeoutMs=30000, **kwargs)}
        completed = subprocess.run([storage_exe, "--config", private_json(base / "storage.json", config)],
                                   capture_output=True, text=True, timeout=35)
        result = json.loads(completed.stdout)
        assert result["ok"] == success, (operation, result, completed.stderr)
        return result

    @contextlib.contextmanager
    def server(protocol, read_only=False, **kwargs):
        port = free_port()
        settings = dict(protocol=protocol, root=str(root), address="127.0.0.1", port=port,
                        username=username, password=password, readOnly=read_only, **kwargs)
        config = private_json(base / (protocol + ".json"), {"backend": backend, "server": settings})
        process = subprocess.Popen([storage_exe, "--serve", "--config", config],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            # A large external executable can spend several seconds in macOS loader verification.
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise AssertionError((protocol, "server exited", process.communicate()))
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                raise AssertionError((protocol, "server did not listen"))
            yield port
        finally:
            process.terminate()
            try:
                stdout, stderr = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate(timeout=5)
                raise AssertionError((protocol, "managed shutdown timed out"))
            assert password not in stdout + stderr, "credential leaked to diagnostics"
            # Parent shutdown must also close the managed backend listener.
            with socket.socket() as probe:
                probe.settimeout(0.5)
                assert probe.connect_ex(("127.0.0.1", port)) != 0, (protocol, "orphan server")

    def roundtrip(scheme, port, name, **credentials):
        url = f"{scheme}://127.0.0.1:{port}/" + urllib.parse.quote(name)
        uploaded = transfer("upload", url, source, username=username, password=password,
                            expectedSha256=digest, **credentials)
        assert uploaded["sha256"] == digest
        downloaded = transfer("download", url, output, username=username, password=password,
                              expectedSha256=digest, **credentials)
        assert downloaded["sha256"] == digest and output.read_bytes() == payload
        checks.append(scheme + " binary upload/download + SHA256")
        return url

    cert = dict(certificate=str(build_dir / "test-cert.pem"), privateKey=str(build_dir / "test-key.pem"))
    ca = dict(caFile=str(build_dir / "test-ca.pem"))
    with server("webdav") as port:
        url = roundtrip("webdav", port, "파일 arbitrary.dat")
        roundtrip("http", port, "http.bin")
        listing = transfer("list", f"webdav://127.0.0.1:{port}/", output, username=username, password=password)
        assert listing["responseCode"] == 207 and b"multistatus" in output.read_bytes().lower()
        output.write_bytes(b"old")
        transfer("download", url, output, username=username, password="wrong", success=False)
        assert output.read_bytes() == b"old"
        # Standard HTTP access to the same WebDAV export.
        transfer("download", url.replace("webdav:", "http:"), output, username=username, password=password,
                 expectedSha256=digest)
        assert output.read_bytes() == payload
        # Named NAS backend must support upload, list, download and byte-level check.
        obscured = subprocess.check_output([rclone, "obscure", password], text=True).strip()
        remote = base / "remotes.conf"
        remote.write_text(f"[nas]\ntype = webdav\nurl = http://127.0.0.1:{port}/\nvendor = other\nuser = {username}\npass = {obscured}\n")
        remote.chmod(0o600)
        storage("copy-file", source=str(source), destination="nas:backend.bin", remote_config=remote)
        listing = storage("list", source="nas:", remote_config=remote)["data"]
        assert any(item["Name"] == "backend.bin" for item in listing)
        storage("copy-file", source="nas:backend.bin", destination=str(base / "backend-download.bin"), remote_config=remote)
        assert (base / "backend-download.bin").read_bytes() == payload
        storage("remove-file", source="nas:backend.bin", remote_config=remote)
        checks.append("named WebDAV NAS backend copy/list/download/remove")
        # A symlink inside the export must not disclose a file outside it.
        (base / "outside.txt").write_text("outside-secret")
        (root / "escape.txt").symlink_to(base / "outside.txt")
        transfer("download", f"http://127.0.0.1:{port}/escape.txt", output,
                 username=username, password=password, success=False)
        (root / "escape.txt").unlink()
        checks.append("wrong credentials and symlink escape rejected")

    with server("webdavs", **cert) as port:
        url = roundtrip("webdavs", port, "tls.bin", **ca)
        output.write_bytes(b"old")
        transfer("download", url, output, username=username, password=password, success=False)
        assert output.read_bytes() == b"old"
        checks.append("untrusted TLS certificate rejected")

    with server("https", read_only=True, **cert) as port:
        transfer("download", f"https://127.0.0.1:{port}/tls.bin", output,
                 username=username, password=password, expectedSha256=digest, **ca)
        transfer("upload", f"https://127.0.0.1:{port}/forbidden.bin", source,
                 username=username, password=password, success=False, **ca)
        assert not (root / "forbidden.bin").exists()
        checks.append("HTTPS serving and read-only enforcement")
        for version in capabilities["httpVersions"]:
            negotiated = transfer("download", f"https://127.0.0.1:{port}/tls.bin", output,
                                  username=username, password=password, expectedSha256=digest,
                                  httpVersion=version, **ca)["httpVersion"]
            assert negotiated == version or (version == "2" and negotiated == "1.1") \
                or (version == "3" and negotiated in ("2", "1.1")), (version, negotiated)
        checks.append("HTTP version selection reports negotiated version, including HTTP/2 and HTTP/3 fallback")

    with server("ftp") as port:
        roundtrip("ftp", port, "ftp.bin")
        transfer("list", f"ftp://127.0.0.1:{port}/", output, username=username, password=password)
        assert b"ftp.bin" in output.read_bytes()
    with server("ftps", **cert) as port:
        roundtrip("ftps", port, "ftps.bin", **ca)
    with server("ftpes", **cert) as port:
        roundtrip("ftpes", port, "ftpes.bin", **ca)
        # TLS must be mandatory for both the login and the passive data connection.
        import ftplib
        client = ftplib.FTP()
        client.connect("127.0.0.1", port, timeout=3)
        try:
            client.login(username, password)
            raise AssertionError("cleartext FTP login accepted")
        except ftplib.error_perm:
            pass
        finally:
            client.close()
        context = ssl.create_default_context(cafile=ca["caFile"])
        client = ftplib.FTP_TLS(context=context)
        client.connect("127.0.0.1", port, timeout=3)
        client.login(username, password)
        try:
            client.retrbinary("RETR ftpes.bin", lambda _: None)  # no PROT P
            raise AssertionError("cleartext data connection accepted")
        except ftplib.error_perm:
            pass
        finally:
            client.close()
        checks.append("FTPES rejects cleartext login and unprotected data connections")

    # Derive the expected SSH host key from our own test key, not from an untrusted network scan.
    ssh_public = subprocess.check_output(["ssh-keygen", "-y", "-f", str(build_dir / "test-key.pem")], text=True).strip()
    with server("sftp", privateKey=str(build_dir / "test-key.pem")) as port:
        known_hosts = base / "known_hosts"
        known_hosts.write_text(f"[127.0.0.1]:{port} {ssh_public}\n")
        url = roundtrip("sftp", port, "sftp.bin", sshKnownHosts=str(known_hosts))
        transfer("list", f"sftp://127.0.0.1:{port}/", output, username=username, password=password,
                 sshKnownHosts=str(known_hosts))
        assert b"sftp.bin" in output.read_bytes()
        known_hosts.write_text("")
        transfer("download", url, output, username=username, password=password,
                 sshKnownHosts=str(known_hosts), success=False)
        checks.append("SFTP listing and unknown host key rejection")

    # Independent SSH implementation provides the legacy SCP wire protocol.
    import asyncssh

    class SSHFixture(asyncssh.SSHServer):
        def begin_auth(self, _):
            return True

        def password_auth_supported(self):
            return True

        def validate_password(self, user, secret):
            return user == username and secret == password

    loop = asyncio.new_event_loop()
    ssh_thread = threading.Thread(target=loop.run_forever, daemon=True)
    ssh_thread.start()

    async def start_scp():
        return await asyncssh.listen("127.0.0.1", 0, server_factory=SSHFixture,
                                     server_host_keys=[str(build_dir / "test-key.pem")],
                                     sftp_factory=lambda channel: asyncssh.SFTPServer(channel, chroot=str(root)),
                                     allow_scp=True)

    ssh_server = asyncio.run_coroutine_threadsafe(start_scp(), loop).result(5)
    try:
        port = ssh_server.get_port()
        known_hosts.write_text(f"[127.0.0.1]:{port} {ssh_public}\n")
        roundtrip("scp", port, "scp.bin", sshKnownHosts=str(known_hosts))
    finally:
        async def stop_scp():
            ssh_server.close()
            await ssh_server.wait_closed()
        asyncio.run_coroutine_threadsafe(stop_scp(), loop).result(5)
        loop.call_soon_threadsafe(loop.stop)
        ssh_thread.join(3)
        loop.close()

    (root / "bucket").mkdir()
    with server("s3s", **cert) as port:
        roundtrip("https", port, "bucket/object.bin", awsSigV4="aws:amz:us-east-1:s3", **ca)
        transfer("download", f"https://127.0.0.1:{port}/bucket/object.bin", output,
                 username=username, password="wrong", awsSigV4="aws:amz:us-east-1:s3", success=False, **ca)
        checks.append("S3-compatible TLS server and AWS SigV4 auth")

    def rest_request(port, method, path, body=None):
        token = base64.b64encode(f"{username}:{password}".encode()).decode()
        req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=body, method=method,
                                     headers={"Authorization": "Basic " + token})
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.read()

    with server("restic") as port:
        rest_request(port, "POST", "/backup/?create=true", b"")
        rest_request(port, "POST", "/backup/config", payload)
        assert rest_request(port, "GET", "/backup/config") == payload
        checks.append("restic REST repository create/upload/download")

    # NFSv3 RPC interoperability without a privileged OS mount. The test speaks only
    # MOUNT, LOOKUP and READ; production protocol implementation remains in rclone.
    def xdr_bytes(value):
        return struct.pack("!I", len(value)) + value + b"\0" * (-len(value) % 4)

    def rpc(port, program, procedure, args):
        xid = 42
        request = struct.pack("!10I", xid, 0, 2, program, 3, procedure, 0, 0, 0, 0) + args
        with socket.create_connection(("127.0.0.1", port), timeout=5) as conn:
            conn.sendall(struct.pack("!I", 0x80000000 | len(request)) + request)

            def read_exact(count):
                result = b""
                while len(result) < count:
                    data = conn.recv(count - len(result))
                    assert data, "short RPC response"
                    result += data
                return result

            response = b""
            while True:
                marker = struct.unpack("!I", read_exact(4))[0]
                response += read_exact(marker & 0x7fffffff)
                if marker & 0x80000000:
                    break
        assert struct.unpack("!3I", response[:12]) == (xid, 1, 0), response[:32]
        verifier_length = struct.unpack("!I", response[16:20])[0]
        offset = 20 + (verifier_length + 3) // 4 * 4
        assert struct.unpack("!I", response[offset:offset + 4])[0] == 0, response[:32]
        return response[offset + 4:]

    with server("nfs", read_only=True) as port:
        mount = rpc(port, 100005, 1, xdr_bytes(b"/"))
        assert struct.unpack("!I", mount[:4])[0] == 0, mount
        length = struct.unpack("!I", mount[4:8])[0]
        directory_handle = mount[8:8 + length]
        lookup = rpc(port, 100003, 3, xdr_bytes(directory_handle) + xdr_bytes(b"scp.bin"))
        assert struct.unpack("!I", lookup[:4])[0] == 0, lookup
        length = struct.unpack("!I", lookup[4:8])[0]
        file_handle = lookup[8:8 + length]
        received = b""
        while len(received) < len(payload):
            response = rpc(port, 100003, 6, xdr_bytes(file_handle) + struct.pack("!QI", len(received), 65536))
            assert struct.unpack("!I", response[:4])[0] == 0, response[:32]
            offset = 8 + (84 if struct.unpack("!I", response[4:8])[0] else 0)
            count, eof, size = struct.unpack("!3I", response[offset:offset + 12])
            assert count == size and count > 0, response[:32]
            received += response[offset + 12:offset + 12 + size]
            if eof:
                break
        assert received == payload
        checks.append("NFSv3 MOUNT/LOOKUP/READ full binary file")

    (root / "nfs-write.bin").write_bytes(payload)
    with server("nfs") as port:
        mount = rpc(port, 100005, 1, xdr_bytes(b"/"))
        assert struct.unpack("!I", mount[:4])[0] == 0
        length = struct.unpack("!I", mount[4:8])[0]
        lookup = rpc(port, 100003, 3, xdr_bytes(mount[8:8 + length]) + xdr_bytes(b"nfs-write.bin"))
        assert struct.unpack("!I", lookup[:4])[0] == 0
        length = struct.unpack("!I", lookup[4:8])[0]
        changed = b"updated-through-nfs\0"
        result = rpc(port, 100003, 7, xdr_bytes(lookup[8:8 + length])
                     + struct.pack("!QII", 0, len(changed), 2) + xdr_bytes(changed))
        assert struct.unpack("!I", result[:4])[0] == 0, result[:32]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not (root / "nfs-write.bin").read_bytes().startswith(changed):
            time.sleep(0.05)
        assert (root / "nfs-write.bin").read_bytes() == changed + payload[len(changed):]
        checks.append("NFSv3 stable WRITE persists through VFS cache")

    port = free_port()
    smb = subprocess.Popen([sys.executable, "-B", str(Path(__file__).with_name("smb_fixture.py")), str(root), str(port)],
                           env=dict(os.environ, FIXTURE_USER=username, FIXTURE_PASSWORD=password),
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            assert smb.poll() is None, "SMB fixture exited"
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise AssertionError("SMB fixture did not listen")
        remote.write_text(f"[nas]\ntype = smb\nhost = 127.0.0.1\nport = {port}\nuser = {username}\npass = {obscured}\ndomain = WORKGROUP\n")
        remote.chmod(0o600)
        storage("copy-file", source=str(source), destination="nas:files/smb.bin", remote_config=remote)
        assert (root / "smb.bin").read_bytes() == payload
        storage("copy-file", source="nas:files/smb.bin", destination=str(base / "smb-downloaded.bin"), remote_config=remote)
        assert (base / "smb-downloaded.bin").read_bytes() == payload
        listed = storage("list", source="nas:files", remote_config=remote)["data"]
        assert any(item["Name"] == "smb.bin" for item in listed)
        storage("remove-file", source="nas:files/smb.bin", remote_config=remote)
        checks.append("authenticated SMB2 NAS upload/download/list/remove")
    finally:
        smb.terminate()
        try:
            smb.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            smb.kill(); smb.communicate(timeout=3)

    # Generic storage operation can verify full file contents, even without a common backend hash.
    a, b = base / "a", base / "b"
    a.mkdir(); b.mkdir(); (a / "model.bin").write_bytes(payload)
    storage("copy-directory", source=str(a), destination=str(b))
    storage("check", source=str(a), destination=str(b))
    (b / "model.bin").write_bytes(b"corrupt")
    storage("check", source=str(a), destination=str(b), success=False)
    checks.append("directory transfer and full-content verification detects corruption")

    # Minimal RFC 1350 fixture: deliberately no extensions, exercise libcurl's TFTP interoperability.
    failures = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("127.0.0.1", 0)); udp.settimeout(10)
        port = udp.getsockname()[1]

        def tftp_fixture():
            try:
                saved = b""
                for _ in range(2):
                    request, client = udp.recvfrom(2048)
                    op = struct.unpack("!H", request[:2])[0]
                    if op == 2:
                        chunks = []
                        udp.sendto(struct.pack("!HH", 4, 0), client)
                        block = 1
                        while True:
                            data, sender = udp.recvfrom(2048)
                            assert sender == client and struct.unpack("!HH", data[:4]) == (3, block)
                            chunks.append(data[4:]); udp.sendto(struct.pack("!HH", 4, block), client)
                            if len(data) < 516:
                                break
                            block += 1
                        saved = b"".join(chunks)
                        assert saved == payload
                    elif op == 1:
                        for offset in range(0, len(saved) + 1, 512):
                            block = offset // 512 + 1
                            udp.sendto(struct.pack("!HH", 3, block) + saved[offset:offset + 512], client)
                            ack, sender = udp.recvfrom(2048)
                            assert sender == client and ack == struct.pack("!HH", 4, block)
                    else:
                        raise AssertionError(op)
            except BaseException as error:
                failures.append(repr(error))

        worker = threading.Thread(target=tftp_fixture, daemon=True); worker.start()
        roundtrip("tftp", port, "tftp.bin")
        worker.join(3)
        assert not worker.is_alive() and not failures, failures

    for scheme in ("gopher", "gophers"):
        with socket.socket() as tcp:
            tcp.bind(("127.0.0.1", 0)); tcp.listen(); tcp.settimeout(10)
            port = tcp.getsockname()[1]

            def gopher_fixture():
                try:
                    connection, _ = tcp.accept()
                    with connection:
                        if scheme == "gophers":
                            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                            context.load_cert_chain(cert["certificate"], cert["privateKey"])
                            connection = context.wrap_socket(connection, server_side=True)
                        with connection:
                            selector = b""
                            while not selector.endswith(b"\r\n"):
                                data = connection.recv(1024)
                                assert data, "incomplete Gopher selector"
                                selector += data
                            connection.sendall(payload)
                            if scheme == "gophers":
                                connection.settimeout(5)
                                connection.unwrap().close()  # TLS close_notify, not a truncated stream.
                except BaseException as error:
                    failures.append(repr(error))

            worker = threading.Thread(target=gopher_fixture, daemon=True); worker.start()
            transfer("download", f"{scheme}://127.0.0.1:{port}/9/file.bin", output,
                     expectedSha256=digest, **(ca if scheme == "gophers" else {}))
            worker.join(3)
            assert output.read_bytes() == payload and not failures, failures
            checks.append(scheme + " binary download")

    print(json.dumps({"ok": True, "checks": checks, "bytesPerTransfer": len(payload)}, ensure_ascii=False))

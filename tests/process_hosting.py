"""Exercise the installed-style relay/host executables with a real HTTP authority."""
import base64
from contextlib import ExitStack
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import threading

relay_exe, host_exe, build = sys.argv[1:]


class Authority(BaseHTTPRequestHandler):
    def do_GET(self):
        cookie = self.headers.get('Cookie', '')
        account = {'sub': 'alice'} if cookie == 'account=alice' else {'sub': 'bob'} if cookie == 'account=bob' else None
        body = json.dumps({'account': account}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def stop(process):
    process.terminate()
    try:
        process.wait(3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(3)


def line(process):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        if not selector.select(10):
            raise AssertionError('Process did not become ready')
        return process.stdout.readline().strip()


with tempfile.TemporaryDirectory(dir=build, prefix='process-hosting-') as directory, ExitStack() as cleanup:
    root = Path(directory)
    share = root / 'files'
    share.mkdir()
    payload = b'iiServerHost cross-process file bytes\0\xff'
    (share / 'sample.bin').write_bytes(payload)
    for account in ('alice', 'bob'):
        path = root / (account + '.cookie')
        path.write_text('account=' + account)
        path.chmod(0o600)
    authority = ThreadingHTTPServer(('127.0.0.1', 0), Authority)
    cleanup.callback(authority.server_close)
    cleanup.callback(authority.shutdown)
    threading.Thread(target=authority.serve_forever, daemon=True).start()
    relay = subprocess.Popen([relay_exe, '--port', '0', '--session-url', f'http://127.0.0.1:{authority.server_port}/Account/Session'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    cleanup.callback(stop, relay)
    port = int(line(relay).rsplit(':', 1)[1])
    common = [host_exe, '--relay', f'ws://127.0.0.1:{port}', '--credential-file', str(root / 'alice.cookie')]
    for remote in (False, True):
        flags = ['--no-local'] if remote else []
        host = subprocess.Popen(common + ['--id', 'host', '--root', str(share)] + flags, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert json.loads(line(host))['ready']
            client = subprocess.run(common + ['--id', 'client', '--peer', 'host', '--read', '--path', 'sample.bin'] + flags, capture_output=True, text=True, timeout=12)
            assert client.returncode == 0, client.stderr + client.stdout
            result = json.loads(client.stdout)
            assert result['transport'] == ('remote' if remote else 'local'), result
            assert base64.b64decode(result['result']['data']) == payload
            print('PASS independent processes:', result['transport'])
        finally:
            stop(host)
    print('PASS HTTP authority -> relay -> two independent peers -> exact file bytes')

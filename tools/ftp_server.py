"""iiServerHost's optional TLS FTP host. Protocol parsing is owned by pyftpdlib.

Requires pyftpdlib >= 2.2 and pyOpenSSL >= 26. Called by FileProtocolServer,
with service credentials only in its private child environment.
"""
import argparse
import logging
import os
from pathlib import Path
import signal

from OpenSSL import SSL
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import TLS_FTPHandler
from pyftpdlib.servers import FTPServer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--address", default="127.0.0.1")
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--certificate", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--passive-ports", default="30000-32000")
    parser.add_argument("--public-address", default="")
    parser.add_argument("--read-only", action="store_true")
    parser.add_argument("--implicit", action="store_true")
    args = parser.parse_args()
    user = os.environ.pop("IISERVERHOST_FTP_USER", "")
    password = os.environ.pop("IISERVERHOST_FTP_PASSWORD", "")
    root = Path(args.root)
    if not user or not password or not root.is_absolute() or not root.is_dir() or root.is_symlink():
        raise SystemExit("authentication and an existing absolute root are required")
    if not 1 <= args.port <= 65535:
        raise SystemExit("invalid port")
    first, last = map(int, args.passive_ports.split("-"))
    if not 1 <= first <= last <= 65535:
        raise SystemExit("invalid passive port range")
    logging.basicConfig(level=logging.ERROR)
    authorizer = DummyAuthorizer()
    authorizer.add_user(user, password, str(root), perm="elr" if args.read_only else "elradfmwMT")

    class ProtectedHandler(TLS_FTPHandler):
        tls_control_required = True
        tls_data_required = True
        banner = "iiServerHost TLS FTP"
        timeout = 120

        @classmethod
        def get_ssl_context(cls):
            if cls.ssl_context is None:
                context = super().get_ssl_context()
                context.set_min_proto_version(SSL.TLS1_2_VERSION)
            return cls.ssl_context

        def handle(self):
            if args.implicit:
                # Defer the FTP greeting until the initial TLS handshake completes.
                self._implicit_greeting = True
                self.secure_connection(self.ssl_context)
            else:
                super().handle()

        def handle_ssl_established(self):
            if getattr(self, "_implicit_greeting", False):
                self._implicit_greeting = False
                super().handle()

    ProtectedHandler.authorizer = authorizer
    ProtectedHandler.certfile = args.certificate
    ProtectedHandler.keyfile = args.key
    ProtectedHandler.passive_ports = range(first, last + 1)
    if args.public_address:
        ProtectedHandler.masquerade_address = args.public_address
    server = FTPServer((args.address, args.port), ProtectedHandler)
    server.max_cons = 64
    server.max_cons_per_ip = 8
    stopping = False

    def stop(*_):
        nonlocal stopping
        stopping = True

    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, stop)
    try:
        while not stopping:
            server.serve_forever(timeout=0.25, blocking=False, handle_exit=False)
    finally:
        server.close_all()


if __name__ == "__main__":
    main()

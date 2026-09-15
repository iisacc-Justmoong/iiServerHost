"""Loopback-only SMB2 interoperability fixture; no host users or system shares."""
import logging
import os
from pathlib import Path
import sys

from impacket.ntlm import compute_lmhash, compute_nthash
from impacket import smb3structs as smb2
from impacket.nt_errors import STATUS_NO_SUCH_FILE, STATUS_OBJECT_NAME_NOT_FOUND, STATUS_END_OF_FILE
from impacket.smbserver import SimpleSMBServer

logging.getLogger("impacket").setLevel(logging.CRITICAL)
root, port = Path(sys.argv[1]).resolve(), int(sys.argv[2])
user, password = os.environ["FIXTURE_USER"], os.environ["FIXTURE_PASSWORD"]
server = SimpleSMBServer(listenAddress="127.0.0.1", listenPort=port)
server.setSMB2Support(True)
server.addShare("files", str(root))
server.addCredential(user, 1000, compute_lmhash(password), compute_nthash(password))

# Impacket 0.13.1's experimental server returns a cleared session ID on LOGOFF,
# so a conforming client cannot match the reply to its session (MS-SMB2 3.2.5.4).
# Its missing-file CREATE status also differs from Windows/Samba, and READ at
# EOF returns a success structure with an error status instead of SMB2Error.
# Keep these fixture-only corrections explicit; production uses the unmodified rclone SMB
# client and does not run or depend on this server.
def create(conn_id, smb_server, request):
    commands, packets, status = original_create(conn_id, smb_server, request)
    if status == STATUS_NO_SUCH_FILE:
        status = STATUS_OBJECT_NAME_NOT_FOUND
    return commands, packets, status


def logoff(conn_id, smb_server, request):
    commands, _, status = original_logoff(conn_id, smb_server, request)
    response = smb2.SMB2Packet()
    response["Flags"] = smb2.SMB2_FLAGS_SERVER_TO_REDIR
    response["Status"] = status
    for name in ("CreditRequestResponse", "Command", "CreditCharge", "Reserved",
                 "SessionID", "MessageID", "TreeID"):
        response[name] = request[name]
    response["Data"] = commands[0].getData()
    return None, [response], status


def read(conn_id, smb_server, request):
    commands, packets, status = original_read(conn_id, smb_server, request)
    if status == STATUS_END_OF_FILE:
        return [smb2.SMB2Error()], None, status
    return commands, packets, status


original_create = server.getServer().hookSmb2Command(smb2.SMB2_CREATE, create)
original_logoff = server.getServer().hookSmb2Command(smb2.SMB2_LOGOFF, logoff)
original_read = server.getServer().hookSmb2Command(smb2.SMB2_READ, read)
server.start()

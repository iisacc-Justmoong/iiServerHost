"""Prepare the pinned LGPL test server with signed LOGOFF responses intact."""
import hashlib
from pathlib import Path
import sys
import urllib.request
import zipfile

COMMIT = '582635541d2ec0c84eac5f66cbebd41e2d35c7ae'
SHA256 = 'a75a1cf74d6b1f40203f3011b6c7c2d1a554baf70e909de3765b00ea2ae249f8'
directory = Path(sys.argv[1]).resolve()
if not directory.is_relative_to(Path(__file__).resolve().parents[2]/'build'):
    raise RuntimeError('The SMB fixture source must remain inside this project build/ directory')
directory.mkdir(parents=True, exist_ok=True)
archive = directory/'source.zip'
if not archive.exists():
    with urllib.request.urlopen(f'https://codeload.github.com/TalAloni/SMBLibrary/zip/{COMMIT}', timeout=60) as response:
        archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('SMBLibrary source digest mismatch')
with zipfile.ZipFile(archive) as bundle:
    for item in bundle.infolist():
        if not (directory/item.filename).resolve().is_relative_to(directory):
            raise RuntimeError('Invalid archive path')
    bundle.extractall(directory)
source = directory/f'SMBLibrary-{COMMIT}'/'SMBLibrary/Server/SMBServer.SMB2.cs'
content = source.read_text(encoding='utf-8-sig')
remove = '                    state.RemoveSession(command.Header.SessionID);'
enqueue = '                EnqueueResponseChain(state, responseChain);'
if content.count(remove) != 1 or content.count(enqueue) != 1:
    raise RuntimeError('SMBLibrary LOGOFF patch no longer matches the pinned source')
# Upstream removes the signing key before serializing LOGOFF. Keep the session
# until the signed response is queued, then perform the original cleanup.
content = content.replace(remove+'\n', '')
content = content.replace(enqueue, enqueue+'''
                foreach (SMB2Command response in responseChain)
                {
                    if (response is LogoffResponse && response.Header.Status == NTStatus.STATUS_SUCCESS)
                        ((SMB2ConnectionState)state).RemoveSession(response.Header.SessionID);
                }''')
source.write_text(content, encoding='utf-8')
print('Prepared pinned SMBLibrary source and deferred LOGOFF session cleanup')

"""Prepare verified protocol test dependencies under this repository's build/ only."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.request
import venv
import zipfile

VERSION = "1.75.1"
repository = Path(__file__).resolve().parent.parent
dependencies = repository / "build" / "protocol-deps"
dependencies.mkdir(parents=True, exist_ok=True)
system = {"Darwin": "osx", "Linux": "linux", "Windows": "windows"}.get(platform.system())
arch = {"arm64": "arm64", "aarch64": "arm64", "x86_64": "amd64", "AMD64": "amd64"}.get(platform.machine())
if not system or not arch:
    raise SystemExit("Select an official rclone build for this platform manually.")
folder = f"rclone-v{VERSION}-{system}-{arch}"
archive = dependencies / (folder + ".zip")
url = f"https://downloads.rclone.org/v{VERSION}/"


def download(source, destination):
    # urllib validates HTTPS and the official download is not executed until its checksum matches.
    with urllib.request.urlopen(source, timeout=60) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


sums = dependencies / f"rclone-v{VERSION}-SHA256SUMS"
download(url + "SHA256SUMS", sums)
expected = next((line.split()[0] for line in sums.read_text().splitlines()
                 if line.split() and line.split()[-1] == archive.name), None)
if expected is None:
    raise SystemExit("Official checksum for this archive was not found.")
if not archive.exists():
    download(url + archive.name, archive)
actual = hashlib.sha256(archive.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit("rclone archive checksum mismatch; remove the archive and retry.")
with zipfile.ZipFile(archive) as bundle:
    for item in bundle.infolist():
        target = (dependencies / item.filename).resolve()
        if not target.is_relative_to(dependencies.resolve()):
            raise SystemExit("Invalid path in release archive.")
    bundle.extractall(dependencies)
executable = dependencies / folder / ("rclone.exe" if system == "windows" else "rclone")
executable.chmod(0o755)
environment = dependencies / "venv"
if not (environment / "pyvenv.cfg").exists():
    venv.create(environment, with_pip=True)
python = environment / ("Scripts/python.exe" if system == "windows" else "bin/python")
subprocess.run([python, "-m", "pip", "install", "--no-cache-dir", "--disable-pip-version-check",
                "-r", str(repository / "tests/protocol-requirements.txt")], check=True,
               env=dict(os.environ, PIP_CACHE_DIR=str(dependencies / "pip-cache")))
version = subprocess.check_output([executable, "version"], text=True, timeout=30)
evidence = {"rclone": str(executable), "python": str(python), "archiveSha256": actual, "version": version}
(dependencies / "verified.json").write_text(json.dumps(evidence, indent=2) + "\n")
print(json.dumps(evidence, indent=2))

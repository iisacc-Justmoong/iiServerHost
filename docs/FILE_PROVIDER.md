# File creation ownership in iiWebServer

Confined uploads, streaming transfer output and platform file-share creation use provider-owned native descriptors/devices. Server routing, TLS, grants and transport validation remain here.

The Python TLS FTP host supplies `ProviderFilesystem` to pyftpdlib. STOR/APPE,
STOU temporary files, MKD and rename operations use the common native-backed
provider. Build and installed runtime directories carry the adapter, native
shared library and notices. External rclone processes remain delegated protocol
engines; their internals are outside the repository source factory audit.

The common [ownership contract](../../iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `src/FileShare.cpp`
- `src/Transfer.cpp`
- `src/WindowsFileShare.h`

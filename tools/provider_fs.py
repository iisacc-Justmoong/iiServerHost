"""pyftpdlib filesystem hooks; protocol and grants remain with the FTP host."""
import os

from pyftpdlib.filesystems import AbstractedFS
import ii_file_provider as provider


class _NamedStream:
    def __init__(self, stream, name):
        self.file = stream
        self.name = name

    def __getattr__(self, key):
        return getattr(self.file, key)


class ProviderFilesystem(AbstractedFS):
    def open(self, filename, mode):
        return _NamedStream(provider.open(filename, mode), filename)

    def mkdir(self, path):
        provider.mkdir(path)

    def rename(self, source, destination):
        provider.rename(source, destination)

    def mkstemp(self, suffix="", prefix="", dir=None, mode="wb"):
        descriptor, name = provider.mkstemp(suffix=suffix, prefix=prefix, dir=dir,
                                           text="b" not in mode)
        try:
            stream = provider.open(descriptor, mode)
        except BaseException:
            os.close(descriptor)
            os.unlink(name)
            raise

        return _NamedStream(stream, name)

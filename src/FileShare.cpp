#include "ServerHost.h"
#include "Protocol.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#ifdef Q_OS_UNIX
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace iiServerHost {
namespace {
bool relative(const QString &path) {
    if (path.size() > 4096 || !path.isValidUtf16() || QDir::isAbsolutePath(path) || path.contains('\\') || path.contains(':') || path.contains(QChar::Null)) return false;
    if (path.isEmpty()) return true;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.startsWith(".iiserverhost-")) return false;
    return true;
}
#ifdef Q_OS_UNIX
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
int confined(const QString &root, const QString &path, quint64 device, quint64 inode, bool directory) {
    int fd = ::open(QFile::encodeName(root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat stat{};
    if (::fstat(fd, &stat) != 0 || quint64(stat.st_dev) != device || quint64(stat.st_ino) != inode) { ::close(fd); return -1; }
    const auto parts = path.isEmpty() ? QStringList() : path.split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | ((directory || i + 1 < parts.size()) ? O_DIRECTORY : 0);
        const int next = ::openat(fd, QFile::encodeName(parts[i]).constData(), flags);
        ::close(fd); fd = next; if (fd < 0) return -1;
    }
    return fd;
}
QString version(const struct stat &s) {
#ifdef Q_OS_DARWIN
    const auto time = s.st_mtimespec;
#else
    const auto time = s.st_mtim;
#endif
    return QString("%1:%2:%3:%4:%5").arg(quint64(s.st_dev)).arg(quint64(s.st_ino)).arg(s.st_size).arg(time.tv_sec).arg(time.tv_nsec);
}
#endif
}
FileShare::FileShare(QString root, std::function<bool()> guard) : m_guard(std::move(guard)) {
    QFileInfo info(root);
    if (QDir::isAbsolutePath(root) && info.isDir() && !info.isSymLink() && info.canonicalFilePath() == QDir::cleanPath(root)) m_root = info.canonicalFilePath();
#ifdef Q_OS_UNIX
    struct stat s{};
    if (!m_root.isEmpty() && ::lstat(QFile::encodeName(m_root).constData(), &s) == 0) { m_device = s.st_dev; m_inode = s.st_ino; }
#endif
}
QString FileShare::resolve(const QString &path, bool allowMissing) const {
    if (m_root.isEmpty() || !relative(path) || (m_guard && !m_guard()) || QFileInfo(m_root).canonicalFilePath() != m_root) return {};
    QString current = m_root;
    const auto parts = path.isEmpty() ? QStringList() : path.split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        current = QDir(current).filePath(parts[i]); const QFileInfo info(current);
        if (info.isSymLink() || (info.exists() && info.canonicalFilePath() != current)
            || (!info.exists() && !(allowMissing && i + 1 == parts.size()))) return {};
    }
    return current;
}
QJsonObject FileShare::handle(const QJsonObject &request) const {
    const auto op = request.value("op").toString(), path = request.value("path").toString();
    if (request.contains("path") && !request.value("path").isString()) return protocol::error("invalid_path");
    const auto absolute = resolve(path, op == "write" || op == "mkdir");
    if (absolute.isEmpty()) return protocol::error("invalid_or_unavailable_path");
#ifdef Q_OS_UNIX
    if (op == "list") {
        bool valid = true; const qint64 offset = request.contains("cursor") ? request.value("cursor").toString().toLongLong(&valid) : 0;
        if (!valid || offset < 0 || offset > 1000000) return protocol::error("invalid_cursor");
        const int fd = confined(m_root, path, m_device, m_inode, true); if (fd < 0) return protocol::error("not_directory");
        DIR *directory = ::fdopendir(fd); if (!directory) { ::close(fd); return protocol::error("not_directory"); }
        QJsonArray entries; qint64 index = 0; bool more = false;
        while (const auto *entry = ::readdir(directory)) {
            const QByteArray name(entry->d_name); const auto text = QFile::decodeName(name);
            if (name == "." || name == ".." || !relative(text)) continue;
            struct stat s{};
            if (::fstatat(fd, name.constData(), &s, AT_SYMLINK_NOFOLLOW) != 0 || (!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode))) continue;
            if (index++ < offset) continue;
            if (entries.size() == 256) { more = true; break; }
            entries.append(QJsonObject{{"name", text}, {"directory", S_ISDIR(s.st_mode)}, {"size", QString::number(s.st_size)}, {"version", version(s)}});
        }
        ::closedir(directory);
        return {{"ok", true}, {"entries", entries}, {"nextCursor", more ? QString::number(offset + entries.size()) : QString()}};
    }
    if (op == "read" || op == "stat") {
        Fd fd(confined(m_root, path, m_device, m_inode, false)); struct stat s{};
        if (fd.value < 0 || ::fstat(fd.value, &s) != 0 || !S_ISREG(s.st_mode)) return protocol::error("not_file");
        const auto revision = version(s);
        if (request.contains("version") && request.value("version").toString() != revision) return protocol::error("file_changed");
        if (op == "stat") return {{"ok", true}, {"size", QString::number(s.st_size)}, {"version", revision}};
        bool valid = true; const qint64 offset = request.contains("offset") ? request.value("offset").toString().toLongLong(&valid) : 0;
        if (!valid || offset < 0 || offset > s.st_size) return protocol::error("invalid_offset");
        QByteArray bytes(qMin(ChunkBytes, s.st_size - offset), Qt::Uninitialized);
        const auto count = ::pread(fd.value, bytes.data(), size_t(bytes.size()), offset);
        struct stat after{};
        if (count < 0 || ::fstat(fd.value, &after) != 0) return protocol::error("read_failed");
        if (version(after) != revision) return protocol::error("file_changed");
        bytes.resize(count);
        return {{"ok", true}, {"data", QString::fromLatin1(bytes.toBase64())}, {"offset", QString::number(offset)},
            {"size", QString::number(s.st_size)}, {"version", revision}, {"eof", offset + count == s.st_size}};
    }
    if (op == "write" || op == "mkdir") {
        if (path.isEmpty()) return protocol::error("invalid_path");
        const auto parent = path.contains('/') ? path.left(path.lastIndexOf('/')) : QString();
        const auto name = QFile::encodeName(path.section('/', -1));
        Fd directory(confined(m_root, parent, m_device, m_inode, true));
        if (directory.value < 0) return protocol::error("not_directory");
        if (op == "mkdir") return ::mkdirat(directory.value, name.constData(), 0700) == 0 ? QJsonObject{{"ok", true}} : protocol::error("create_failed_or_exists");
        const auto encoded = request.value("data").toString().toLatin1();
        if (encoded.size() > ((ChunkBytes + 2) / 3) * 4) return protocol::error("write_too_large");
        const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (!request.value("data").isString() || !decoded || decoded.decoded.size() > ChunkBytes) return protocol::error("invalid_data");
        const auto temporary = QByteArray(".iiserverhost-") + protocol::uuid().toLatin1();
        Fd fd(::openat(directory.value, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (fd.value < 0) return protocol::error("write_failed");
        qint64 written = 0;
        while (written < decoded.decoded.size()) {
            const auto count = ::write(fd.value, decoded.decoded.constData() + written, size_t(decoded.decoded.size() - written));
            if (count <= 0) break; written += count;
        }
        const bool ok = written == decoded.decoded.size() && ::fsync(fd.value) == 0
            && ::linkat(directory.value, temporary.constData(), directory.value, name.constData(), 0) == 0;
        ::unlinkat(directory.value, temporary.constData(), 0);
        return ok ? QJsonObject{{"ok", true}, {"size", QString::number(written)}} : protocol::error("write_failed_or_exists");
    }
#else
    // Until native no-follow handles are implemented, fail closed on platforms
    // where Qt alone cannot guarantee confinement during a concurrent rename.
    Q_UNUSED(op);
    return protocol::error("filesystem_hosting_unsupported_platform");
#endif
    return protocol::error("unsupported_operation");
}
}

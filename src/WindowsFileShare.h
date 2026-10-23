#include <iiFileProviderStorage.h>
#include <iiFileProviderPresentation.h>
#pragma once
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memory>
#include <vector>

namespace iiWebServer::windows_files {
inline bool safeEntry(const QString& name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.startsWith(".iiwebserver-")
        && !iiFileProvider::native::Presentation::isControlFile(std::filesystem::path(name.toStdU16String()))
        && !name.contains('/') && !name.contains('\\') && !name.contains(':') && !name.contains(QChar::Null);
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
inline std::unique_ptr<Handle> open(const QString& path, DWORD access, bool directory) {
    const auto native = QDir::toNativeSeparators(path);
    auto result = std::make_unique<Handle>(iiFileProvider::native::createFile(reinterpret_cast<LPCWSTR>(native.utf16()), access,
        FILE_SHARE_READ | (directory ? FILE_SHARE_WRITE : 0), nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (result->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(result->value, &info)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        || bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) return {};
    return result;
}
inline quint64 id(const BY_HANDLE_FILE_INFORMATION& info) {
    return (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
}
inline QString version(const BY_HANDLE_FILE_INFORMATION& info) {
    const auto ticks = (quint64(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    const auto size = (quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return QString("%1:%2:%3:%4").arg(info.dwVolumeSerialNumber).arg(id(info)).arg(size).arg(ticks);
}
// Keep every parent open without delete sharing while operating on descendants.
struct Path {
    std::vector<std::unique_ptr<Handle>> parents;
    QString absolute;
    bool pin(const QString& root, const QString& relative, quint64 device, quint64 inode) {
        auto first = open(root, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, true);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!first || !GetFileInformationByHandle(first->value, &info)
            || info.dwVolumeSerialNumber != device || id(info) != inode) return false;
        parents.push_back(std::move(first)); absolute = root;
        if (!relative.isEmpty()) for (const auto& part : relative.split('/')) {
            absolute = QDir(absolute).filePath(part);
            auto parent = open(absolute, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, true);
            if (!parent) return false;
            parents.push_back(std::move(parent));
        }
        return true;
    }
};
inline QJsonObject handle(const QString& root, quint64 device, quint64 inode,
                         const QJsonObject& request) {
    const auto op = request.value("op").toString(), path = request.value("path").toString();
    const auto parent = path.contains('/') ? path.left(path.lastIndexOf('/')) : QString();
    Path pinned;
    if (!pinned.pin(root, op == "list" ? path : parent, device, inode)) return protocol::error("not_directory");
    const auto absolute = path.isEmpty() ? root : QDir(root).filePath(path);
    if (op == "list") {
        bool valid = true;
        const qint64 offset = request.contains("cursor") ? request.value("cursor").toString().toLongLong(&valid) : 0;
        if (!valid || offset < 0 || offset > 1000000) return protocol::error("invalid_cursor");
        const auto pattern = QDir::toNativeSeparators(absolute + "/*");
        WIN32_FIND_DATAW entry{}; const auto search = FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()), &entry);
        if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND
            ? QJsonObject{{"ok", true}, {"entries", QJsonArray{}}, {"nextCursor", ""}}
            : protocol::error("list_failed");
        struct Search { HANDLE value; ~Search() { FindClose(value); } } close{search};
        QJsonArray entries; qint64 index = 0; bool more = false;
        do {
            const auto name = QString::fromWCharArray(entry.cFileName);
            if (!safeEntry(name) || (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (index++ < offset) continue;
            if (entries.size() == 256) { more = true; break; }
            const bool directory = entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
            auto child = open(QDir(absolute).filePath(name), FILE_READ_ATTRIBUTES, directory);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!child || !GetFileInformationByHandle(child->value, &info)) continue;
            entries.append(QJsonObject{{"name", name}, {"directory", directory},
                {"size", QString::number((quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow)}, {"version", version(info)}});
        } while (FindNextFileW(search, &entry));
        return {{"ok", true}, {"entries", entries}, {"nextCursor", more ? QString::number(offset + entries.size()) : QString()}};
    }
    if (op == "read" || op == "stat") {
        auto file = open(absolute, GENERIC_READ, false); BY_HANDLE_FILE_INFORMATION info{};
        if (!file || !GetFileInformationByHandle(file->value, &info)) return protocol::error("not_file");
        const auto revision = version(info);
        const auto size = (quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (request.contains("version") && request.value("version").toString() != revision) return protocol::error("file_changed");
        if (op == "stat") return {{"ok", true}, {"size", QString::number(size)}, {"version", revision}};
        bool valid = true;
        const qint64 offset = request.contains("offset") ? request.value("offset").toString().toLongLong(&valid) : 0;
        if (!valid || offset < 0 || quint64(offset) > size) return protocol::error("invalid_offset");
        LARGE_INTEGER position{}; position.QuadPart = offset;
        if (!SetFilePointerEx(file->value, position, nullptr, FILE_BEGIN)) return protocol::error("read_failed");
        QByteArray bytes(qMin<quint64>(FileShare::ChunkBytes, size - offset), Qt::Uninitialized); DWORD count = 0;
        if (!ReadFile(file->value, bytes.data(), DWORD(bytes.size()), &count, nullptr)) return protocol::error("read_failed");
        bytes.resize(count);
        return {{"ok", true}, {"data", QString::fromLatin1(bytes.toBase64())}, {"offset", QString::number(offset)},
            {"size", QString::number(size)}, {"version", revision}, {"eof", quint64(offset) + count == size}};
    }
    const auto native = QDir::toNativeSeparators(absolute);
    const auto destination = reinterpret_cast<LPCWSTR>(native.utf16());
    if (path.isEmpty()) return protocol::error("invalid_path");
    if (op == "mkdir") return iiFileProvider::native::createDirectoryWindows(destination, nullptr)
        ? QJsonObject{{"ok", true}} : protocol::error("create_failed_or_exists");
    if (op == "write") {
        const auto encoded = request.value("data").toString().toLatin1();
        if (encoded.size() > ((FileShare::ChunkBytes + 2) / 3) * 4) return protocol::error("write_too_large");
        const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (!request.value("data").isString() || !decoded || decoded.decoded.size() > FileShare::ChunkBytes) return protocol::error("invalid_data");
        const auto temporary = QDir::toNativeSeparators(QDir(pinned.absolute).filePath(".iiwebserver-" + protocol::uuid()));
        const auto source = reinterpret_cast<LPCWSTR>(temporary.utf16());
        const auto file = iiFileProvider::native::createFile(source, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return protocol::error("write_failed");
        DWORD written = 0;
        const bool stored = WriteFile(file, decoded.decoded.constData(), DWORD(decoded.decoded.size()), &written, nullptr)
            && written == DWORD(decoded.decoded.size()) && FlushFileBuffers(file);
        const bool published = stored && CreateHardLinkW(destination, source, nullptr);
        CloseHandle(file); DeleteFileW(source);
        if (published) iiFileProvider::native::Presentation::enqueue(std::filesystem::path(absolute.toStdU16String()));
        return published ? QJsonObject{{"ok", true}, {"size", QString::number(written)}} : protocol::error("write_failed_or_exists");
    }
    return protocol::error("unsupported_operation");
}
}
#endif

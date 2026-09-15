#pragma once
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cstdio>

namespace cli {
inline QJsonObject readPrivateObject(const QString &path, QString &error) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > 64 * 1024) { error = "invalid_config_file"; return {}; }
#ifdef Q_OS_UNIX
    if (info.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)) {
        error = "private_config_required"; return {};
    }
#endif
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "config_read_failed"; return {}; }
    const auto data = file.read(64 * 1024 + 1);
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(data, &parse);
    if (data.size() > 64 * 1024 || parse.error != QJsonParseError::NoError || !doc.isObject()) { error = "invalid_config_json"; return {}; }
    return doc.object();
}
inline void print(const QJsonObject &value) {
    const auto data = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
    std::fflush(stdout);
}
inline int failure(const QString &error) { print({{"ok", false}, {"error", error}}); return 2; }
}

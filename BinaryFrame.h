#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <array>

namespace iiServerHost::protocol {
// Private local transport codec. Only the three existing file-data fields can
// become binary. Pairing/control messages and relay traffic keep JSON framing.
class BinaryFrame final {
    static auto paths() {
        return std::array<QStringList, 3>{{{"payload", "message", "data"}, {"result", "result", "data"}, {"result", "data"}}};
    }
    static QJsonValue at(QJsonObject object, const QStringList &path) {
        for (int i = 0; i + 1 < path.size(); ++i) {
            if (!object.value(path[i]).isObject()) return {};
            object = object.value(path[i]).toObject();
        }
        return object.value(path.last());
    }
    static QJsonObject replace(QJsonObject object, const QStringList &path, int index, const QJsonValue &value) {
        if (index + 1 == path.size()) {
            if (value.isUndefined()) object.remove(path[index]); else object.insert(path[index], value);
        } else object.insert(path[index], replace(object.value(path[index]).toObject(), path, index + 1, value));
        return object;
    }
public:
    static constexpr qsizetype MaximumPayload = 256 * 1024, MaximumHeader = 64 * 1024;
    static QByteArray encode(const QJsonObject &message) {
        const auto candidates = paths();
        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto value = at(message, candidates[i]); if (!value.isString()) continue;
            const auto text = value.toString().toLatin1();
            if (text.size() > (MaximumPayload + 2) / 3 * 4) continue;
            const auto data = QByteArray::fromBase64Encoding(text, QByteArray::AbortOnBase64DecodingErrors);
            if (!data || data.decoded.isEmpty() || data.decoded.size() > MaximumPayload || data.decoded.toBase64() != text) continue;
            const auto header = QJsonDocument(QJsonObject{{"path", int(i)},
                {"message", replace(message, candidates[i], 0, QJsonValue(QJsonValue::Undefined))}}).toJson(QJsonDocument::Compact);
            if (header.size() > MaximumHeader) return {};
            QByteArray frame("ISB1"); const auto size = qToBigEndian(quint32(header.size()));
            frame.append(reinterpret_cast<const char *>(&size), sizeof(size)); frame += header; frame += data.decoded;
            return frame;
        }
        return {};
    }
    static QJsonObject decode(const QByteArray &frame) {
        if (frame.size() < 9 || frame.size() > MaximumHeader + MaximumPayload + 8 || !frame.startsWith("ISB1")) return {};
        const auto size = qFromBigEndian<quint32>(frame.constData() + 4);
        if (!size || size > MaximumHeader || size >= frame.size() - 8 || frame.size() - 8 - size > MaximumPayload) return {};
        QJsonParseError error; const auto document = QJsonDocument::fromJson(frame.mid(8, size), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
        const auto header = document.object(); const auto path = header.value("path").toInt(-1); const auto candidates = paths();
        if (header.size() != 2 || path < 0 || path >= int(candidates.size()) || header.value("path").toDouble() != path
            || !header.value("message").isObject()) return {};
        const auto message = header.value("message").toObject();
        auto parent = message;
        for (int i = 0; i + 1 < candidates[path].size(); ++i) {
            if (!parent.value(candidates[path][i]).isObject()) return {};
            parent = parent.value(candidates[path][i]).toObject();
        }
        if (parent.contains("data")) return {};
        return replace(message, candidates[path], 0, QString::fromLatin1(frame.mid(8 + size).toBase64()));
    }
};
}

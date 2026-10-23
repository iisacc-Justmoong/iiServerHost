#pragma once
#include "WebServer.h"
#include "BinaryFrame.h"
#include "NetworkTransport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace iiWebServer::protocol {
inline constexpr qint64 WireLimit = 1024 * 1024;
inline QJsonObject error(const QString &code) { return {{"ok", false}, {"error", code}}; }
inline QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
inline bool identifier(const QString &s) {
    static const QRegularExpression pattern("\\A[a-zA-Z0-9_.:-]{1,128}\\z");
    return pattern.match(s).hasMatch();
}
inline bool loopback(const QString &host) { const auto address = Address::parse(host.toStdString()); return address && address->is_loopback(); }
inline bool endpoint(const QUrl &url, const QString &secure, const QString &plain) {
    return url.isValid() && !url.host().isEmpty() && url.port() != 0
        && !url.hasFragment() && !url.hasQuery() && url.userInfo().isEmpty()
        && (url.scheme() == secure || (url.scheme() == plain && loopback(url.host())));
}
inline bool live(const Principal &p) {
    return !p.accountId.isEmpty() && p.accountId.size() <= 256 && p.expiresAt > QDateTime::currentDateTimeUtc();
}
inline bool send(WebSocket *socket, const QJsonObject &message) {
    if (!socket || socket->state() != ii::network::SocketState::connected) return false;
    const auto frame = socket->property("iish.binary").toBool() ? BinaryFrame::encode(message) : QByteArray();
    if (!frame.isEmpty()) {
        if (socket->bytesToWrite() > 4 * WireLimit) { socket->close(1009, "Capacity exceeded"); return false; }
        return socket->sendBinaryMessage(frame) >= 0;
    }
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (bytes.size() > WireLimit || socket->bytesToWrite() > 4 * WireLimit) {
        socket->close(1009, "Capacity exceeded"); return false;
    }
    return socket->sendTextMessage(QString::fromUtf8(bytes)) >= 0;
}
inline QJsonObject parse(const QString &text) {
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(text.toUtf8(), &e);
    return e.error == QJsonParseError::NoError && d.isObject() ? d.object() : QJsonObject();
}
inline std::unique_ptr<WebSocketServer> listener(const Address &address, quint16 port,
                                               const TlsConfiguration &tls, QString &error) {
    if (!hasIdentity(tls) && !address.is_loopback()) { error = "TLS certificate and key are required for non-loopback listeners."; return {}; }
    auto server = std::make_unique<WebSocketServer>();
    if (!server->listen(address, port, tls)) { error = server->errorString(); return {}; }
    return server;
}
inline bool pinCertificate(TlsConfiguration &tls, const QString &fingerprint) {
    const auto bytes = QByteArray::fromHex(fingerprint.toLatin1());
    if (fingerprint.size() != 64 || bytes.size() != 32 || bytes.toHex() != fingerprint.toLatin1()) return false;
    std::array<unsigned char, 32> pin{}; std::copy(bytes.begin(), bytes.end(), pin.begin());
    tls.policy.trusted_certificate_sha256 = pin; return true;
}
} // namespace iiWebServer::protocol

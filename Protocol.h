#pragma once
#include "ServerHost.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSslKey>
#include <QSslSocket>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QUuid>

namespace iiServerHost::protocol {
inline constexpr qint64 WireLimit = 1024 * 1024;
inline QJsonObject error(const QString &code) { return {{"ok", false}, {"error", code}}; }
inline QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
inline bool identifier(const QString &s) {
    static const QRegularExpression pattern("\\A[a-zA-Z0-9_.:-]{1,128}\\z");
    return pattern.match(s).hasMatch();
}
inline bool loopback(const QString &host) { return QHostAddress(host).isLoopback(); }
inline bool endpoint(const QUrl &url, const QString &secure, const QString &plain) {
    return url.isValid() && !url.host().isEmpty() && url.port() != 0
        && !url.hasFragment() && !url.hasQuery() && url.userInfo().isEmpty()
        && (url.scheme() == secure || (url.scheme() == plain && loopback(url.host())));
}
inline bool live(const Principal &p) {
    return !p.accountId.isEmpty() && p.accountId.size() <= 256 && p.expiresAt > QDateTime::currentDateTimeUtc();
}
inline void limits(QWebSocket *socket) {
    socket->setMaxAllowedIncomingMessageSize(WireLimit);
    socket->setMaxAllowedIncomingFrameSize(WireLimit);
    socket->setProxy(QNetworkProxy::NoProxy);
}
inline bool send(QWebSocket *socket, const QJsonObject &message) {
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return false;
    if (bytes.size() > WireLimit || socket->bytesToWrite() > 4 * WireLimit) {
        socket->close(QWebSocketProtocol::CloseCodeTooMuchData, "Capacity exceeded"); return false;
    }
    return socket->sendTextMessage(QString::fromUtf8(bytes)) >= 0;
}
inline QJsonObject parse(const QString &text) {
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(text.toUtf8(), &e);
    return e.error == QJsonParseError::NoError && d.isObject() ? d.object() : QJsonObject();
}
inline std::unique_ptr<QWebSocketServer> listener(const QHostAddress &address, quint16 port,
                                               const QSslConfiguration &tls, QString &error) {
#ifdef Q_OS_MACOS
    // Qt Secure Transport otherwise imports server keys into the login keychain.
    if (!qEnvironmentVariableIsSet("QT_SSL_USE_TEMPORARY_KEYCHAIN")) qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1");
#endif
    const bool secure = !tls.localCertificate().isNull() && !tls.privateKey().isNull();
    if (!secure && !address.isLoopback()) { error = "TLS certificate and key are required for non-loopback listeners."; return {}; }
    auto server = std::make_unique<QWebSocketServer>("iiServerHost/1", secure ? QWebSocketServer::SecureMode : QWebSocketServer::NonSecureMode);
    if (secure) {
        auto config = tls;
        config.setProtocol(QSsl::TlsV1_2OrLater);
        config.setPeerVerifyMode(QSslSocket::VerifyNone); // Clients authenticate using a one-use relay ticket.
        server->setSslConfiguration(config);
    }
    server->setMaxPendingConnections(64);
    server->setHandshakeTimeout(5000);
    if (!server->listen(address, port)) { error = server->errorString(); return {}; }
    return server;
}
} // namespace iiServerHost::protocol

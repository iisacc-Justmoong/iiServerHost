#pragma once

#include "iiServerHostExport.h"
#include <QDateTime>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QSslConfiguration>
#include <QUrl>
#include <functional>
#include <memory>

namespace iiServerHost {

struct IISERVERHOST_EXPORT Principal {
    QString accountId;
    QDateTime expiresAt;
};
using AuthCompletion = std::function<void(Principal)>;
// The trusted authority derives accountId from a credential, never from client JSON.
using Authenticator = std::function<void(const QByteArray &, AuthCompletion)>;
using RequestHandler = std::function<QJsonObject(const QString &peerId, const QJsonObject &)>;

class IISERVERHOST_EXPORT SessionAuthenticator final : public QObject {
    Q_OBJECT
public:
    explicit SessionAuthenticator(QUrl sessionUrl, QObject *parent = nullptr);
    ~SessionAuthenticator() override;
    void authenticate(const QByteArray &cookie, AuthCompletion completion);
private:
    class Private;
    std::unique_ptr<Private> d;
};

class IISERVERHOST_EXPORT RelayServer final : public QObject {
    Q_OBJECT
public:
    explicit RelayServer(Authenticator authenticate, QObject *parent = nullptr);
    ~RelayServer() override;
    // Plaintext is accepted only when bound to a literal loopback address.
    bool listen(const QHostAddress &address, quint16 port = 0,
                const QSslConfiguration &tls = {});
    void stop();
    quint16 port() const;
    QString errorString() const;
private:
    class Private;
    std::unique_ptr<Private> d;
};

struct IISERVERHOST_EXPORT PeerOptions {
    QUrl relayUrl;
    QByteArray credential; // Cookie header for the trusted account authority; memory only.
    QString peerId;
    QString name;
    QString service = QStringLiteral("files");
    QJsonObject metadata;
    bool hostFiles = true;
    bool localEnabled = true;
    bool localHostingEnabled = true;
    QHostAddress listenAddress = QHostAddress::AnyIPv4;
    quint16 localPort = 0;
    QSslConfiguration localTls;
    QSslConfiguration relayTls = QSslConfiguration::defaultConfiguration();
    QStringList localAddresses; // Empty: enumerate active, non-loopback IPv4 interfaces.
    int localTimeoutMs = 1200;
    int requestTimeoutMs = 15000;
};

// A consuming app can host and browse with the same Peer. All callbacks run on
// its Qt thread. Local TLS is pinned to the authenticated account directory.
class IISERVERHOST_EXPORT Peer final : public QObject {
    Q_OBJECT
public:
    explicit Peer(QObject *parent = nullptr);
    ~Peer() override;
    bool start(const PeerOptions &options, RequestHandler handler = {});
    void stop();
    bool isReady() const;
    QJsonArray peers() const;
    QString accountId() const;
    QString errorString() const;
    quint16 localPort() const;
    void refreshPeers();
    // Request completion is asynchronous, including validation errors. A request
    // sent to a host is never replayed automatically across transports.
    QString request(const QString &peerId, const QJsonObject &payload);
    // A QR offer is single-use and bound to the live host/account/service.
    QString createPairingOffer();
    QString claimPairingOffer(const QString &code, const QString &expectedHost);
    void confirmPairing(const QString &pairingId);
    void cancelPairing(const QString &requestId);
signals:
    void stateChanged();
    void peersChanged();
    void completed(QString requestId, QJsonObject result, QString transport);
    void pairingEvent(QJsonObject event);
private:
    class Private;
    std::unique_ptr<Private> d;
};

// Ordinary filesystem share. Root and every component are checked on each call.
// An optional application guard can additionally pin a container UUID/layout.
class IISERVERHOST_EXPORT FileShare {
public:
    explicit FileShare(QString root, std::function<bool()> guard = {});
    QJsonObject handle(const QJsonObject &request) const;
    static constexpr qint64 ChunkBytes = 256 * 1024;
private:
    QString resolve(const QString &relative, bool allowMissing = false) const;
    QString m_root;
    std::function<bool()> m_guard;
    quint64 m_device = 0, m_inode = 0;
};

} // namespace iiServerHost

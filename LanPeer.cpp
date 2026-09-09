#include "LanPeer.h"
#include "Protocol.h"
#ifdef IISERVERHOST_LAN_HOST
#include "LanIdentity.h"
#endif
#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QPointer>
#include <QRandomGenerator>
#include <QSslError>
#include <QTimer>
#include <QTimeZone>
#include <QUrlQuery>
#include <algorithm>

namespace iiServerHost {
using namespace protocol;
namespace {
bool hexToken(const QString &text) {
    static const QRegularExpression pattern("\\A[a-f0-9]{64}\\z");
    return pattern.match(text).hasMatch();
}
bool validLink(const LanLink &link) {
    if (link.addresses.isEmpty() || link.addresses.size() > 8 || !link.port || !identifier(link.hostId)
        || link.name.isEmpty() || link.name.size() > 128 || !hexToken(link.fingerprint) || !hexToken(link.code)
        || !link.expiresAt.isValid()) return false;
    for (const auto &address : link.addresses) if (!LanLink::localAddress(address)) return false;
    return true;
}
QString randomCode() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); i += 4) { const auto value = QRandomGenerator::system()->generate(); memcpy(bytes.data() + i, &value, 4); }
    return QString::fromLatin1(bytes.toHex());
}
bool sameCode(const QString &a, const QString &b) {
    const auto first = a.toLatin1(), second = b.toLatin1();
    if (first.size() != 64 || second.size() != 64) return false;
    unsigned diff = 0; for (int i = 0; i < 64; ++i) diff |= static_cast<unsigned char>(first[i] ^ second[i]);
    return diff == 0;
}
QStringList lanAddresses() {
    auto interfaces = QNetworkInterface::allInterfaces();
    std::stable_sort(interfaces.begin(), interfaces.end(), [](const auto &a, const auto &b) {
        const auto rank = [](const auto &i) { return (i.type() == QNetworkInterface::Wifi || i.type() == QNetworkInterface::Ethernet) ? 0 : 1; };
        return rank(a) < rank(b);
    });
    QStringList result;
    for (const auto &interface : interfaces) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack) || flags.testFlag(QNetworkInterface::IsPointToPoint)) continue;
        for (const auto &entry : interface.addressEntries()) {
            const auto text = entry.ip().toString();
            if (!entry.ip().isLoopback() && LanLink::localAddress(text) && !result.contains(text)) result.append(text);
        }
    }
    return result.mid(0, 8);
}
}
bool LanLink::localAddress(const QString &text) {
    const QHostAddress address(text);
    if (address.protocol() != QAbstractSocket::IPv4Protocol || text != address.toString()) return false;
    // Link-local metadata services are not a LAN pairing destination.
    return address.isLoopback() || address.isInSubnet(QHostAddress("10.0.0.0"), 8)
        || address.isInSubnet(QHostAddress("172.16.0.0"), 12) || address.isInSubnet(QHostAddress("192.168.0.0"), 16);
}
QString LanLink::encode() const {
    if (!validLink(*this)) return {};
    QUrl url("society://pair"); QUrlQuery query;
    query.addQueryItem("v", "2"); query.addQueryItem("addresses", addresses.join(','));
    query.addQueryItem("port", QString::number(port)); query.addQueryItem("host", hostId);
    query.addQueryItem("name", QString::fromLatin1(QUrl::toPercentEncoding(name)));
    query.addQueryItem("fingerprint", fingerprint); query.addQueryItem("code", code);
    query.addQueryItem("expires", QString::number(expiresAt.toMSecsSinceEpoch()));
    url.setQuery(query); return url.toString(QUrl::FullyEncoded);
}
bool LanLink::decode(const QString &text, LanLink *result) {
    if (!result || text.size() > 2048) return false;
    const QUrl url(text, QUrl::StrictMode); const QUrlQuery query(url);
    if (!url.isValid() || url.scheme() != "society" || url.host() != "pair" || !url.path().isEmpty()
        || !url.userInfo().isEmpty() || url.port() != -1 || url.hasFragment()) return false;
    QSet<QString> keys; const auto items = query.queryItems(); for (const auto &item : items) keys.insert(item.first);
    if (items.size() != 8 || keys != QSet<QString>{"v", "addresses", "port", "host", "name", "fingerprint", "code", "expires"}
        || query.queryItemValue("v") != "2") return false;
    const auto value = [&](const QString &key) { return query.queryItemValue(key, QUrl::FullyDecoded); };
    bool portOk, timeOk; LanLink link;
    link.addresses = value("addresses").split(','); link.port = value("port").toUShort(&portOk);
    link.hostId = value("host"); link.name = value("name"); link.fingerprint = value("fingerprint"); link.code = value("code");
    link.expiresAt = QDateTime::fromMSecsSinceEpoch(value("expires").toLongLong(&timeOk), QTimeZone::UTC);
    if (!portOk || !timeOk || !validLink(link)) return false;
    *result = link; return true;
}

class LanPeer::Private {
public:
    struct Session { QString id, name; bool paired = false; QDateTime deadline; };
    LanPeer *q;
    std::unique_ptr<QWebSocketServer> server;
    QPointer<QWebSocket> client;
    QHash<QWebSocket *, Session> clients;
    QHash<QString, QDateTime> requests;
    RequestHandler handler;
    LanLink offer, joining;
    QString phase = "idle", error, qr, peerName, clientId, clientName, connectionError;
    QTimer timer;
    QDateTime deadline, connectDeadline;
    int addressIndex = 0;
    bool clientPaired = false, helloSent = false, stopping = false;
    explicit Private(LanPeer *owner) : q(owner) {
        timer.setInterval(250); QObject::connect(&timer, &QTimer::timeout, q, [this] { sweep(); });
    }
    void fail(QString message) {
        error = std::move(message); phase = "error"; qr.clear(); joining.code.clear(); clientPaired = false;
        if (client) { auto *socket = client.data(); client = nullptr; socket->disconnect(q); socket->abort(); socket->deleteLater(); }
        const auto pending = requests.keys(); requests.clear();
        for (const auto &id : pending) emit q->completed(id, protocol::error("connection_lost"), "local");
        emit q->changed();
    }
    bool pinned(QWebSocket *socket) const {
        const auto cert = socket->sslConfiguration().peerCertificate();
        return !cert.isNull() && QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex()) == joining.fingerprint;
    }
    void openAddress() {
        if (client) { auto *old = client.data(); client = nullptr; old->disconnect(q); old->abort(); old->deleteLater(); }
        if (addressIndex >= joining.addresses.size()) { fail("Cannot reach the desktop. Use the same Wi-Fi or LAN and allow Society in the firewall. " + connectionError); return; }
        auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, q); client = socket; limits(socket);
        auto tls = QSslConfiguration::defaultConfiguration(); tls.setProtocol(QSsl::TlsV1_2OrLater); tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket->setSslConfiguration(tls); helloSent = false;
        QObject::connect(socket, &QWebSocket::sslErrors, q, [this, socket](const QList<QSslError> &errors) {
            if (!pinned(socket)) { fail("The desktop certificate does not match the QR code."); return; }
            for (const auto &e : errors) {
                if (e.error() != QSslError::SelfSignedCertificate && e.error() != QSslError::SelfSignedCertificateInChain
                    && e.error() != QSslError::HostNameMismatch && e.error() != QSslError::UnableToGetLocalIssuerCertificate
                    && e.error() != QSslError::UnableToVerifyFirstCertificate && e.error() != QSslError::CertificateUntrusted) {
                    fail("The desktop TLS certificate is invalid."); return;
                }
            }
            socket->ignoreSslErrors(errors); // Trust comes from the scanned fingerprint, before any application bytes.
        });
        QObject::connect(socket, &QWebSocket::connected, q, [this, socket] {
            if (!pinned(socket)) { fail("The desktop certificate does not match the QR code."); return; }
            helloSent = true; phase = "verifying";
            send(socket, {{"type", "pair"}, {"host", joining.hostId}, {"code", joining.code}, {"peerId", clientId}, {"name", clientName}});
            joining.code.clear(); emit q->changed();
        });
        QObject::connect(socket, &QWebSocket::textMessageReceived, q, [this](const QString &text) { clientMessage(parse(text)); });
        QObject::connect(socket, &QWebSocket::errorOccurred, q, [this, socket] {
            connectionError = socket->errorString();
            if (!helloSent && !stopping) openAddress(); else if (!stopping) fail("The local connection failed. Scan a new QR code.");
        });
        QObject::connect(socket, &QWebSocket::disconnected, q, [this] { if (!stopping) fail("The desktop disconnected. Scan a new QR code to reconnect."); });
        QUrl url; url.setScheme("wss"); url.setHost(joining.addresses[addressIndex++]); url.setPort(joining.port);
        connectDeadline = QDateTime::currentDateTimeUtc().addSecs(4); socket->open(url);
    }
    void clientMessage(const QJsonObject &m) {
        const auto type = m.value("type").toString();
        if (type == "error") { fail(m.value("error").toString("Pairing failed. Scan a new QR code.")); return; }
        if (type == "ready" && phase == "verifying" && m.value("host").toString() == joining.hostId) {
            const auto result = m.value("files").toObject();
            if (!result.value("ok").toBool() || !result.value("entries").isArray()) { fail("The desktop could not open its Files. Pairing was not completed."); return; }
            send(client, {{"type", "confirm"}}); return;
        }
        if (type == "paired" && phase == "verifying" && m.value("host").toString() == joining.hostId) {
            clientPaired = true; phase = "paired"; peerName = m.value("name").toString();
            emit q->changed(); emit q->paired(joining.hostId); return;
        }
        if (type == "response" && clientPaired) {
            const auto id = m.value("id").toString(); if (!requests.remove(id)) return;
            emit q->completed(id, m.value("result").toObject(), "local"); return;
        }
        fail("The desktop returned an invalid local pairing response.");
    }
    void hostMessage(QWebSocket *socket, const QJsonObject &m) {
        if (!clients.contains(socket)) return;
        const auto reject = [&](const QString &message) { send(socket, {{"type", "error"}, {"error", message}}); socket->close(); };
        auto &session = clients[socket]; const auto type = m.value("type").toString();
        if (type == "pair" && session.id.isEmpty()) {
            const auto id = m.value("peerId").toString(), name = m.value("name").toString();
            if (qr.isEmpty() || offer.expiresAt <= QDateTime::currentDateTimeUtc() || m.value("host").toString() != offer.hostId
                || !sameCode(m.value("code").toString(), offer.code) || !identifier(id) || id == offer.hostId || name.isEmpty() || name.size() > 128) {
                reject("This QR code is expired, cancelled, or already used. Show a new QR code."); return;
            }
            for (const auto &other : std::as_const(clients)) if (other.id == id) { reject("This device is already connected."); return; }
            // Consume before opening Files or reporting success. No iisacc cookie is involved.
            offer.code.clear(); qr.clear(); session.id = id; session.name = name; session.deadline = QDateTime::currentDateTimeUtc().addSecs(15);
            phase = "verifying"; peerName = name;
            QJsonObject files;
            try { files = handler(id, {{"op", "list"}, {"path", ""}}); } catch (...) { files = protocol::error("host_error"); }
            if (!files.value("ok").toBool() || !files.value("entries").isArray()) { error = "The desktop could not open its Files."; phase = "error"; reject(error); emit q->changed(); return; }
            send(socket, {{"type", "ready"}, {"host", offer.hostId}, {"files", files}}); emit q->changed(); return;
        }
        if (type == "confirm" && !session.id.isEmpty() && !session.paired && session.deadline > QDateTime::currentDateTimeUtc()) {
            session.paired = true; phase = "paired";
            send(socket, {{"type", "paired"}, {"host", offer.hostId}, {"name", offer.name}});
            emit q->changed(); emit q->paired(session.id); return;
        }
        if (type == "request" && session.paired && identifier(m.value("id").toString()) && m.value("payload").isObject()) {
            QJsonObject result;
            try { result = handler(session.id, m.value("payload").toObject()); } catch (...) { result = protocol::error("host_error"); }
            send(socket, {{"type", "response"}, {"id", m.value("id")}, {"result", result}}); return;
        }
        reject("Pair the device before requesting Files.");
    }
    void sweep() {
        const auto now = QDateTime::currentDateTimeUtc();
        if (!qr.isEmpty() && offer.expiresAt <= now) { qr.clear(); offer.code.clear(); phase = "error"; error = "The QR code expired. Show a new code."; emit q->changed(); }
        else if (!qr.isEmpty()) emit q->changed();
        for (auto *socket : clients.keys()) if (!clients.value(socket).paired && clients.value(socket).deadline <= now) socket->close();
        if (client && !clientPaired) {
            if (deadline <= now) fail("Pairing timed out. Check the local network and scan a new QR code.");
            else if (!helloSent && connectDeadline <= now) openAddress();
        }
        for (const auto &id : requests.keys()) if (requests.value(id) <= now) { requests.remove(id); emit q->completed(id, protocol::error("timeout"), "local"); }
    }
};
LanPeer::LanPeer(QObject *parent) : QObject(parent), d(std::make_unique<Private>(this)) {}
LanPeer::~LanPeer() { stop(); }
bool LanPeer::startHost(QString hostId, QString name, RequestHandler handler, QStringList addresses, QHostAddress bindAddress) {
    stop();
#ifndef IISERVERHOST_LAN_HOST
    Q_UNUSED(hostId); Q_UNUSED(name); Q_UNUSED(handler); Q_UNUSED(addresses); Q_UNUSED(bindAddress);
    d->fail("Only desktop Society can host Files."); return false;
#else
    if (addresses.isEmpty()) addresses = lanAddresses();
    if (addresses.isEmpty()) { d->fail("Connect this desktop to Wi-Fi or Ethernet before showing a QR code."); return false; }
    for (const auto &address : addresses) if (!LanLink::localAddress(address)) { d->fail("A private local IPv4 address is required."); return false; }
    if (!identifier(hostId) || name.isEmpty() || name.size() > 128 || !handler) { d->fail("Invalid local host identity."); return false; }
    const auto tls = createLanIdentity(addresses);
    if (tls.localCertificate().isNull() || tls.privateKey().isNull()) { d->fail("Could not create the desktop TLS identity."); return false; }
    d->server = listener(bindAddress, 0, tls, d->error);
    if (!d->server) { d->fail(d->error); return false; }
    d->handler = std::move(handler);
    d->offer = {addresses, d->server->serverPort(), std::move(hostId), std::move(name),
        QString::fromLatin1(tls.localCertificate().digest(QCryptographicHash::Sha256).toHex()), {}, {}};
    connect(d->server.get(), &QWebSocketServer::newConnection, this, [this] {
        while (d->server->hasPendingConnections()) {
            auto *socket = d->server->nextPendingConnection(); socket->setParent(this); limits(socket);
            if (d->clients.size() >= 8 || !LanLink::localAddress(socket->peerAddress().toString())) { socket->close(); socket->deleteLater(); continue; }
            d->clients.insert(socket, {{}, {}, false, QDateTime::currentDateTimeUtc().addSecs(5)});
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) { d->hostMessage(socket, parse(text)); });
            connect(socket, &QWebSocket::disconnected, this, [this, socket] {
                const auto previous = d->clients.take(socket); socket->deleteLater();
                if (!previous.id.isEmpty() && !previous.paired && d->phase == "verifying") { d->phase = "error"; d->error = "Pairing was interrupted. Show a new QR code."; }
                emit changed();
            });
        }
    });
    d->phase = "hosting"; d->timer.start(); emit changed(); return true;
#endif
}
QString LanPeer::createOffer(int lifetimeSeconds) {
    if (!hosting() || lifetimeSeconds < 1 || lifetimeSeconds > 60) return {};
    cancelPairing(); d->error.clear(); d->phase = "showing"; d->offer.code = randomCode();
    d->offer.expiresAt = QDateTime::currentDateTimeUtc().addSecs(lifetimeSeconds); d->qr = d->offer.encode();
    emit changed(); return d->qr;
}
bool LanPeer::join(const QString &qr, QString peerId, QString name) {
    stop(); LanLink link;
    if (!LanLink::decode(qr, &link) || link.expiresAt <= QDateTime::currentDateTimeUtc() || !identifier(peerId)
        || name.isEmpty() || name.size() > 128) { d->fail("This is not a valid, current local Society QR code."); return false; }
    d->joining = link; d->clientId = std::move(peerId); d->clientName = std::move(name); d->phase = "connecting";
    d->deadline = QDateTime::currentDateTimeUtc().addSecs(15); d->addressIndex = 0; d->timer.start();
    d->openAddress(); emit changed(); return true;
}
void LanPeer::cancelPairing() {
    d->qr.clear(); d->offer.code.clear();
    for (auto *socket : d->clients.keys()) if (!d->clients.value(socket).paired) { socket->disconnect(this); d->clients.remove(socket); socket->abort(); socket->deleteLater(); }
    if (d->client && !d->clientPaired) { auto *socket = d->client.data(); d->client = nullptr; socket->disconnect(this); socket->abort(); socket->deleteLater(); }
    d->phase = hosting() ? "hosting" : d->clientPaired ? "paired" : "idle"; d->error.clear(); emit changed();
}
void LanPeer::stop() {
    d->stopping = true; d->timer.stop(); d->server.reset();
    if (d->client) { auto *socket = d->client.data(); d->client = nullptr; socket->disconnect(this); socket->abort(); socket->deleteLater(); }
    for (auto *socket : d->clients.keys()) { socket->disconnect(this); socket->abort(); socket->deleteLater(); }
    d->clients.clear(); d->clientPaired = false; d->handler = {}; d->offer = {}; d->joining = {}; d->qr.clear(); d->error.clear(); d->peerName.clear();
    d->phase = "idle"; d->connectionError.clear(); const auto requests = d->requests.keys(); d->requests.clear();
    for (const auto &id : requests) emit completed(id, protocol::error("stopped"), "local");
    d->stopping = false; emit changed();
}
bool LanPeer::hosting() const { return bool(d->server); }
bool LanPeer::connected() const { return d->clientPaired; }
QString LanPeer::phase() const { return d->phase; }
QString LanPeer::errorString() const { return d->error; }
QString LanPeer::qrText() const { return d->qr; }
QString LanPeer::peerName() const { return d->peerName; }
int LanPeer::secondsRemaining() const { return d->qr.isEmpty() ? 0 : qMax(0, int((QDateTime::currentDateTimeUtc().msecsTo(d->offer.expiresAt) + 999) / 1000)); }
QJsonArray LanPeer::peers() const {
    QJsonArray result;
    if (d->clientPaired) result.append(QJsonObject{{"peerId", d->joining.hostId}, {"name", d->peerName}});
    return result;
}
QString LanPeer::request(const QString &host, const QJsonObject &payload) {
    const auto id = uuid();
    if (!d->clientPaired || host != d->joining.hostId || d->requests.size() >= 32 || QJsonDocument(payload).toJson(QJsonDocument::Compact).size() > WireLimit - 1024) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, protocol::error("not_connected_or_busy"), "local"); }); return id;
    }
    d->requests.insert(id, QDateTime::currentDateTimeUtc().addSecs(15));
    QTimer::singleShot(0, this, [this, id, payload] {
        if (!d->requests.contains(id)) return;
        if (!send(d->client, {{"type", "request"}, {"id", id}, {"payload", payload}})) d->fail("The local connection closed.");
    });
    return id;
}
}

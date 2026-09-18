#include "ServerHost.h"
#include "Protocol.h"
#include <QPointer>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTimer>

namespace iiServerHost {
using namespace protocol;
class RelayServer::Private {
public:
    struct Session {
        Principal principal;
        QString id, service, name;
        QJsonObject descriptor;
        bool authenticating = false;
        QDateTime connected = QDateTime::currentDateTimeUtc();
        QDateTime pairingWindow;
        int pairingAttempts = 0;
    };
    struct Route { QPointer<QWebSocket> client, host; QString id; QDateTime expires; };
    struct Offer { QPointer<QWebSocket> host; QString request; QDateTime expires; };
    struct Pair { QPointer<QWebSocket> host, client; QString hostRequest, clientRequest; QDateTime expires; };
    RelayServer *q;
    Authenticator auth;
    std::unique_ptr<QWebSocketServer> server;
    QHash<QWebSocket *, Session> sessions;
    QHash<QString, Route> routes;
    QHash<QByteArray, Offer> offers; // Only SHA-256 hashes of QR codes are retained.
    QHash<QString, Pair> pairs;
    QTimer timer;
    QString error;
    explicit Private(RelayServer *owner, Authenticator a) : q(owner), auth(std::move(a)) {
        timer.setInterval(500);
        QObject::connect(&timer, &QTimer::timeout, q, [this] { sweep(); });
    }
    bool active(QWebSocket *s) const { return sessions.contains(s) && live(sessions.value(s).principal); }
    void pairing(QWebSocket *s, const QString &id, const QString &status, QJsonObject data = {}) {
        data.insert("type", "pairing"); data.insert("id", id); data.insert("status", status); send(s, data);
    }
    void finishPair(const QString &id, const QString &status) {
        if (!pairs.contains(id)) return;
        const auto p = pairs.take(id);
        if (p.host) pairing(p.host, p.hostRequest, status, {{"pairingId", id}, {"peerId", sessions.value(p.client).id}, {"name", sessions.value(p.client).name}});
        if (p.client) pairing(p.client, p.clientRequest, status, {{"pairingId", id}, {"peerId", sessions.value(p.host).id}, {"name", sessions.value(p.host).name}});
    }
    void cancelPairing(QWebSocket *s, const QString &request = {}) {
        for (const auto &hash : offers.keys()) {
            const auto o = offers.value(hash);
            if (o.host == s && (request.isEmpty() || o.request == request)) {
                offers.remove(hash); pairing(s, o.request, "cancelled");
            }
        }
        for (const auto &id : pairs.keys()) {
            const auto p = pairs.value(id);
            if ((p.host == s && (request.isEmpty() || p.hostRequest == request))
                || (p.client == s && (request.isEmpty() || p.clientRequest == request))) finishPair(id, "cancelled");
        }
    }
    bool pairMessage(QWebSocket *s, const QJsonObject &m) {
        const auto type = m.value("type").toString(), id = m.value("id").toString();
        if (!type.startsWith("pair-")) return false;
        if (type == "pair-confirm") {
            const auto key = m.value("pairingId").toString(); const auto p = pairs.value(key);
            if (!pairs.contains(key) || p.client != s) return true;
            if (p.expires <= QDateTime::currentDateTimeUtc() || !active(p.host) || !active(p.client)
                || target(s, sessions.value(p.host).id) != p.host) finishPair(key, "expired");
            else finishPair(key, "paired");
            return true;
        }
        if (!identifier(id)) { s->close(); return true; }
        if (type == "pair-cancel") { cancelPairing(s, id); return true; }
        auto &caller = sessions[s]; const auto now = QDateTime::currentDateTimeUtc();
        if (!caller.pairingWindow.isValid() || caller.pairingWindow.msecsTo(now) >= 30000) { caller.pairingWindow = now; caller.pairingAttempts = 0; }
        if (++caller.pairingAttempts > 6) { pairing(s, id, "error", {{"error", "pairing_rate_limited"}}); return true; }
        if (type == "pair-offer") {
            if (caller.descriptor.isEmpty()) { pairing(s, id, "error", {{"error", "host_required"}}); return true; }
            cancelPairing(s);
            QByteArray random(32, Qt::Uninitialized);
            for (qsizetype i = 0; i < random.size(); i += 4) { quint32 v = QRandomGenerator::system()->generate(); memcpy(random.data() + i, &v, 4); }
            const auto code = random.toHex(); const auto expires = qMin(now.addSecs(60), caller.principal.expiresAt);
            offers.insert(QCryptographicHash::hash(code, QCryptographicHash::Sha256), {s, id, expires});
            pairing(s, id, "offered", {{"code", QString::fromLatin1(code)}, {"peerId", caller.id}, {"name", caller.name}, {"expires", QString::number(expires.toMSecsSinceEpoch())}});
            return true;
        }
        if (type == "pair-claim") {
            const auto code = m.value("code").toString();
            const auto hash = QCryptographicHash::hash(code.toLatin1(), QCryptographicHash::Sha256);
            const auto offer = offers.value(hash); auto *host = offer.host.data();
            if (code.size() != 64 || !offers.contains(hash) || offer.expires <= now || !host || !active(host)
                || !caller.descriptor.isEmpty() || sessions.value(host).id != m.value("host").toString()
                || target(s, m.value("host").toString()) != host) {
                pairing(s, id, "error", {{"error", "pairing_invalid_or_expired"}}); return true;
            }
            cancelPairing(s); offers.remove(hash); // Consume before notifying either participant.
            const auto key = uuid(); const auto expires = qMin(now.addSecs(15), offer.expires);
            pairs.insert(key, {host, s, offer.request, id, expires});
            pairing(host, offer.request, "claimed", {{"pairingId", key}, {"peerId", caller.id}, {"name", caller.name}, {"expires", QString::number(expires.toMSecsSinceEpoch())}});
            pairing(s, id, "claimed", {{"pairingId", key}, {"peerId", sessions.value(host).id}, {"name", sessions.value(host).name}, {"expires", QString::number(expires.toMSecsSinceEpoch())}});
            return true;
        }
        s->close(); return true;
    }
    QWebSocket *target(QWebSocket *source, const QString &id) const {
        const auto caller = sessions.value(source);
        for (auto it = sessions.cbegin(); it != sessions.cend(); ++it)
            if (it.key() != source && it->id == id && live(it->principal) && !it->descriptor.isEmpty()
                && it->principal.accountId == caller.principal.accountId && it->service == caller.service) return it.key();
        return nullptr;
    }
    void directory(QWebSocket *s) {
        if (!active(s) || sessions[s].id.isEmpty()) return;
        QJsonArray peers;
        for (auto it = sessions.cbegin(); it != sessions.cend(); ++it)
            if (it.key() != s && live(it->principal) && !it->descriptor.isEmpty()
                && it->principal.accountId == sessions[s].principal.accountId && it->service == sessions[s].service)
                peers.append(it->descriptor);
        send(s, {{"type", "peers"}, {"peers", peers}});
    }
    void changed() { const auto all = sessions.keys(); for (auto *s : all) directory(s); }
    void remove(QWebSocket *s) {
        cancelPairing(s);
        sessions.remove(s);
        for (const auto &key : routes.keys()) {
            const auto r = routes.value(key);
            if (r.client == s || r.host == s) {
                routes.remove(key);
                if (r.client && r.client != s) send(r.client, {{"type", "response"}, {"id", r.id}, {"result", protocol::error("host_offline")}});
            }
        }
        s->deleteLater(); changed();
    }
    void sweep() {
        const auto now = QDateTime::currentDateTimeUtc();
        for (const auto &hash : offers.keys()) {
            const auto o = offers.value(hash);
            if (o.expires <= now || !active(o.host)) { offers.remove(hash); if (o.host) pairing(o.host, o.request, "expired"); }
        }
        for (const auto &id : pairs.keys()) {
            const auto p = pairs.value(id);
            if (p.expires <= now || !active(p.host) || !active(p.client)) finishPair(id, "expired");
        }
        for (auto *s : sessions.keys()) {
            const auto state = sessions.value(s);
            if ((!state.principal.accountId.isEmpty() && !live(state.principal))
                || (state.principal.accountId.isEmpty() && state.connected.msecsTo(now) > 7000))
                s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Authentication expired");
            else s->ping();
        }
        for (const auto &id : routes.keys()) if (routes.value(id).expires < now) {
            const auto r = routes.take(id);
            if (r.client) send(r.client, {{"type", "response"}, {"id", r.id}, {"result", protocol::error("timeout")}});
        }
    }
    void message(QWebSocket *s, const QJsonObject &m) {
        if (!sessions.contains(s)) return;
        const auto type = m.value("type").toString();
        if (type == "auth") {
            auto &session = sessions[s];
            const auto credential = m.value("credential").toString().toUtf8();
            if (session.authenticating || credential.isEmpty() || credential.size() > 16384 || !auth) { s->close(); return; }
            session.authenticating = true;
            const QPointer<RelayServer> owner(q); const QPointer<QWebSocket> socket(s);
            auth(credential, [this, owner, socket](Principal p) {
                if (!owner || !socket || !sessions.contains(socket)) return;
                auto &state = sessions[socket]; state.authenticating = false;
                if (!live(p) || (!state.principal.accountId.isEmpty() && p.accountId != state.principal.accountId)) {
                    cancelPairing(socket); state.principal = {};
                    send(socket, {{"type", "error"}, {"error", "unauthenticated"}}); socket->close(); return;
                }
                // Limit even custom verifiers to a one-minute revocation window.
                p.expiresAt = qMin(p.expiresAt, QDateTime::currentDateTimeUtc().addSecs(60));
                state.principal = p;
                send(socket, {{"type", "authenticated"}, {"accountId", p.accountId}});
            }); return;
        }
        if (!active(s)) { s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Authentication required"); return; }
        if (type == "register") {
            const auto id = m.value("peerId").toString(), service = m.value("service").toString();
            if (!identifier(id) || !identifier(service) || m.value("name").toString().size() > 128
                || QJsonDocument(m.value("metadata").toObject()).toJson().size() > 8192) { s->close(); return; }
            for (auto it = sessions.cbegin(); it != sessions.cend(); ++it)
                if (it.key() != s && it->principal.accountId == sessions[s].principal.accountId && it->id == id && it->service == service) {
                    send(s, {{"type", "error"}, {"error", "duplicate_peer"}}); s->close(); return;
                }
            auto &state = sessions[s];
            if ((!state.id.isEmpty() && state.id != id) || (!state.service.isEmpty() && state.service != service)) { s->close(); return; }
            state.id = id; state.service = service; state.name = m.value("name").toString();
            QJsonArray urls;
            const auto fp = m.value("fingerprint").toString();
            for (const auto &value : m.value("localUrls").toArray()) {
                const QUrl url(value.toString());
                if (urls.size() < 16 && endpoint(url, "wss", "ws") && !QHostAddress(url.host()).isNull()
                    && (url.scheme() == "ws" || QRegularExpression("\\A[a-f0-9]{64}\\z").match(fp).hasMatch())) urls.append(url.toString());
            }
            const bool wasHost = !state.descriptor.isEmpty();
            state.descriptor = m.value("host").toBool() ? QJsonObject{{"peerId", id}, {"name", m.value("name")},
                {"service", service}, {"metadata", m.value("metadata")}, {"localUrls", urls}, {"fingerprint", fp}} : QJsonObject();
            if (wasHost && state.descriptor.isEmpty()) cancelPairing(s);
            send(s, {{"type", "registered"}}); changed(); return;
        }
        if (sessions[s].id.isEmpty()) { s->close(); return; }
        if (pairMessage(s, m)) return;
        if (type == "list") { directory(s); return; }
        if (type == "connect") {
            const auto id = m.value("peerId").toString(); auto *host = target(s, id);
            if (!host) { send(s, {{"type", "connect"}, {"peerId", id}, {"error", "host_unavailable"}}); return; }
            QByteArray random(32, Qt::Uninitialized);
            for (qsizetype i = 0; i < random.size(); i += 4) { quint32 value = QRandomGenerator::system()->generate(); memcpy(random.data() + i, &value, 4); }
            const auto ticket = QString::fromLatin1(random.toHex());
            const auto expiry = qMin(sessions[s].principal.expiresAt, sessions[host].principal.expiresAt).toMSecsSinceEpoch();
            send(host, {{"type", "ticket"}, {"ticket", ticket}, {"peerId", sessions[s].id}, {"expires", QString::number(expiry)}});
            send(s, {{"type", "connect"}, {"peerId", id}, {"ticket", ticket}, {"descriptor", sessions[host].descriptor}, {"expires", QString::number(expiry)}});
            return;
        }
        if (type == "request") {
            const auto id = m.value("id").toString(); auto *host = target(s, m.value("peerId").toString());
            if (!identifier(id) || !m.value("payload").isObject()) { s->close(); return; }
            int count = 0; for (const auto &r : routes) if (r.client == s) ++count;
            if (!host || count >= 32 || routes.size() >= 1024) {
                send(s, {{"type", "response"}, {"id", id}, {"result", protocol::error(host ? "busy" : "host_unavailable")}}); return;
            }
            const auto route = uuid();
            routes.insert(route, {s, host, id, QDateTime::currentDateTimeUtc().addSecs(15)});
            send(host, {{"type", "request"}, {"id", route}, {"peerId", sessions[s].id}, {"payload", m.value("payload")}}); return;
        }
        if (type == "response") {
            const auto key = m.value("id").toString();
            if (!routes.contains(key) || routes.value(key).host != s) return;
            const auto r = routes.take(key);
            if (r.client && active(r.client)) send(r.client, {{"type", "response"}, {"id", r.id}, {"result", m.value("result")}});
            return;
        }
        s->close(QWebSocketProtocol::CloseCodeProtocolError, "Invalid message");
    }
};
RelayServer::RelayServer(Authenticator auth, QObject *parent) : QObject(parent), d(std::make_unique<Private>(this, std::move(auth))) {}
RelayServer::~RelayServer() { stop(); }
bool RelayServer::listen(const QHostAddress &address, quint16 port, const QSslConfiguration &tls) {
    stop(); d->error.clear();
    if (!d->auth) { d->error = "An account authenticator is required."; return false; }
    d->server = listener(address, port, tls, d->error); if (!d->server) return false;
    connect(d->server.get(), &QWebSocketServer::newConnection, this, [this] {
        while (d->server->hasPendingConnections()) {
            auto *s = d->server->nextPendingConnection(); s->setParent(this); limits(s);
            if (d->sessions.size() >= 128) { s->close(); s->deleteLater(); continue; }
            d->sessions.insert(s, {});
            connect(s, &QWebSocket::textMessageReceived, this, [this, s](const QString &m) { d->message(s, parse(m)); });
            connect(s, &QWebSocket::binaryMessageReceived, s, [s] { s->close(); });
            connect(s, &QWebSocket::disconnected, this, [this, s] { d->remove(s); });
        }
    }); d->timer.start(); return true;
}
void RelayServer::stop() {
    d->timer.stop(); d->server.reset();
    const auto sockets = d->sessions.keys(); d->sessions.clear(); d->routes.clear(); d->offers.clear(); d->pairs.clear();
    for (auto *s : sockets) { s->disconnect(this); s->abort(); delete s; }
}
quint16 RelayServer::port() const { return d->server ? d->server->serverPort() : 0; }
QString RelayServer::errorString() const { return d->error; }
}

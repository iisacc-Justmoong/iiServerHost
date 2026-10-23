#include "WebServer.h"
#include "Protocol.h"
#include <QCryptographicHash>
#include <QPointer>
#include <QTimer>
#include <QTimeZone>

namespace iiWebServer {
using namespace protocol;
class Peer::Private {
public:
    struct Pending { QString peer; QJsonObject payload; QString transport; QPointer<WebSocket> socket; QDateTime deadline; };
    struct Ticket { QString peer; QDateTime expires; };
    struct Link { QString peer, ticket, fingerprint; QDateTime expires, deadline; bool outgoing = false, authenticated = false; };
    struct Attempt { QStringList urls; QString ticket, fingerprint; QDateTime expires; };
    Peer *q;
    PeerOptions options;
    RequestHandler handler;
    WebSocket relay;
    std::unique_ptr<WebSocketServer> server;
    QHash<QString, Pending> requests;
    QHash<QString, Ticket> tickets;
    QHash<WebSocket *, Link> links;
    QHash<QString, QPointer<WebSocket>> outbound;
    QHash<QString, Attempt> attempts;
    QSet<QString> connecting;
    QTimer timer, retry, authTimer;
    QJsonArray directory;
    QString account, error;
    bool running = false, ready = false, fatal = false;
    int backoff = 1000;
    explicit Private(Peer *owner) : q(owner) {

        timer.setInterval(100); retry.setSingleShot(true); authTimer.setInterval(30000);
        QObject::connect(&timer, &QTimer::timeout, q, [this] { sweep(); });
        QObject::connect(&retry, &QTimer::timeout, q, [this] { openRelay(); });
        QObject::connect(&authTimer, &QTimer::timeout, q, [this] { authenticate(); });
        QObject::connect(&relay, &WebSocket::connected, q, [this] { authenticate(); authTimer.start(); });
        QObject::connect(&relay, &WebSocket::errorOccurred, q, [this] {
            if (!running) return;
            if (error.isEmpty()) error = relay.errorString(); emit q->stateChanged(); scheduleReconnect();
        });
        QObject::connect(&relay, &WebSocket::disconnected, q, [this] {
            authTimer.stop(); ready = false; directory = {}; account.clear();
            resetLocal();
            for (const auto &id : requests.keys()) finish(id, protocol::error("connection_lost"));
            emit q->stateChanged(); emit q->peersChanged(); scheduleReconnect();
        });
        QObject::connect(&relay, &WebSocket::textMessageReceived, q, [this](const QString &m) { message(parse(m)); });
    }
    void scheduleReconnect() {
        if (running && !fatal && !retry.isActive()) { retry.start(backoff); backoff = qMin(backoff * 2, 30000); }
    }
    void openRelay() {
        if (!running) return;
        relay.setTlsConfiguration(options.relayTls);
        relay.open(options.relayUrl);
    }
    void authenticate() { send(&relay, {{"type", "auth"}, {"credential", QString::fromUtf8(options.credential)}}); }
    void resetLocal() {
        const auto sockets = links.keys(); links.clear(); outbound.clear(); attempts.clear(); connecting.clear(); tickets.clear();
        for (auto *s : sockets) { s->disconnect(q); s->abort(); s->deleteLater(); }
    }
    void finish(const QString &id, const QJsonObject &result) {
        if (!requests.contains(id)) return;
        const auto request = requests.take(id);
        emit q->completed(id, result, request.transport);
    }
    void remote(const QString &peer) {
        connecting.remove(peer); attempts.remove(peer);
        for (const auto &id : requests.keys()) {
            auto &r = requests[id]; if (r.peer != peer || !r.transport.isEmpty()) continue;
            r.transport = "remote";
            if (!send(&relay, {{"type", "request"}, {"id", id}, {"peerId", peer}, {"payload", r.payload}})) finish(id, protocol::error("connection_lost"));
        }
    }
    void local(WebSocket *socket) {
        if (!links.contains(socket) || !links[socket].authenticated) return;
        const auto peer = links[socket].peer;
        connecting.remove(peer); attempts.remove(peer);
        for (const auto &id : requests.keys()) {
            auto &r = requests[id]; if (r.peer != peer || !r.transport.isEmpty()) continue;
            r.transport = "local"; r.socket = socket;
            if (!send(socket, {{"type", "request"}, {"id", id}, {"payload", r.payload}})) finish(id, protocol::error("connection_lost"));
        }
    }
    void startRequest(const QString &id) {
        if (!requests.contains(id)) return;
        const auto peer = requests[id].peer;
        if (!ready) { finish(id, protocol::error("not_connected")); return; }
        bool exists = false; for (const auto &entry : directory) if (entry.toObject().value("peerId") == peer) exists = true;
        if (!exists) { finish(id, protocol::error("host_unavailable")); return; }
        auto *socket = outbound.value(peer).data();
        if (socket && links.contains(socket) && links[socket].authenticated && links[socket].expires > QDateTime::currentDateTimeUtc()) { local(socket); return; }
        if (!options.localEnabled) { remote(peer); return; }
        if (!connecting.contains(peer)) {
            connecting.insert(peer);
            send(&relay, {{"type", "connect"}, {"peerId", peer}});
        }
    }
    bool onLink(const QUrl &url) const {
        const auto address = Address::parse(url.host().toStdString());
        if (!address || address->is_multicast() || address->is_unspecified() || address->string() == "255.255.255.255") return false;
        if (address->is_loopback()) return loopback(options.relayUrl.host());
        for (const auto &entry : ii::network::interfaces()) {
            const auto own = Address::parse(entry.address);
            if (!own || !entry.prefix_length) continue;
            const auto subnet = ii::network::IpSubnet{*own, entry.prefix_length};
            if (subnet.contains(*address)) {
                if (address->family() == ii::network::AddressFamily::ipv4 && entry.prefix_length < 32) {
                    const auto &bytes = address->bytes();
                    const auto host = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) | bytes[3];
                    const auto mask = std::uint32_t(0xffffffffu >> entry.prefix_length);
                    if ((host & mask) == mask) continue;
                }
                return true;
            }
        }
        return false;
    }
    void nextLocal(const QString &peer) {
        if (!running || !ready || !connecting.contains(peer)) return;
        auto &a = attempts[peer];
        if (a.urls.isEmpty() || a.expires <= QDateTime::currentDateTimeUtc()) { remote(peer); return; }
        const QUrl url(a.urls.takeFirst());
        auto *s = new WebSocket(q);
        links.insert(s, {peer, a.ticket, a.fingerprint, a.expires, QDateTime::currentDateTimeUtc().addMSecs(options.localTimeoutMs), true, false});
        outbound.insert(peer, s);
        if (url.scheme() == "wss") {
            TlsConfiguration tls;
            if (!pinCertificate(tls, a.fingerprint)) { lost(s); return; }
            s->setTlsConfiguration(std::move(tls));
        }
        QObject::connect(s, &WebSocket::connected, q, [this, s, url] {
            if (!links.contains(s)) return;
            if (url.scheme() == "wss" && s->peerCertificateFingerprint() != links[s].fingerprint) { s->abort(); return; }
            send(s, {{"type", "hello"}, {"ticket", links[s].ticket}});
        });
        attach(s);
        QObject::connect(s, &WebSocket::errorOccurred, q, [this, s] { lost(s); });
        s->open(url);
    }
    void lost(WebSocket *s) {
        if (!links.contains(s)) return;
        const auto l = links.take(s);
        if (outbound.value(l.peer) == s) outbound.remove(l.peer);
        s->disconnect(q); s->abort(); s->deleteLater();
        for (const auto &id : requests.keys()) if (requests[id].socket == s) finish(id, protocol::error("connection_lost"));
        if (l.outgoing && !l.authenticated) QTimer::singleShot(0, q, [this, peer = l.peer] { nextLocal(peer); });
    }
    QJsonObject handle(const QString &peer, const QJsonObject &payload) {
        if (!ready || !options.hostFiles || !handler) return protocol::error("not_hosting");
        try { return handler(peer, payload); } catch (...) { return protocol::error("host_error"); }
    }
    void attach(WebSocket *s) {
        QObject::connect(s, &WebSocket::disconnected, q, [this, s] { lost(s); });
        QObject::connect(s, &WebSocket::binaryMessageReceived, s, [s] { s->close(); });
        QObject::connect(s, &WebSocket::textMessageReceived, q, [this, s](const QString &text) {
            if (!links.contains(s)) return;
            auto &l = links[s]; const auto m = parse(text); const auto type = m.value("type").toString();
            if (!l.authenticated) {
                if (l.outgoing && type == "hello" && m.value("ok").toBool()) { l.authenticated = true; local(s); return; }
                const auto ticket = m.value("ticket").toString();
                if (!l.outgoing && type == "hello" && ready && tickets.contains(ticket) && tickets[ticket].expires > QDateTime::currentDateTimeUtc()) {
                    const auto t = tickets.take(ticket); l.peer = t.peer; l.expires = t.expires; l.authenticated = true;
                    send(s, {{"type", "hello"}, {"ok", true}}); return;
                }
                s->close(1008, "Ticket required"); return;
            }
            if (!ready || l.expires <= QDateTime::currentDateTimeUtc()) { s->close(); return; }
            if (type == "request" && !l.outgoing && identifier(m.value("id").toString()) && m.value("payload").isObject()) {
                const auto result = handle(l.peer, m.value("payload").toObject());
                send(s, {{"type", "response"}, {"id", m.value("id")}, {"result", result}}); return;
            }
            const auto id = m.value("id").toString();
            if (type == "response" && l.outgoing && requests.contains(id) && requests[id].socket == s) { finish(id, m.value("result").toObject()); return; }
            s->close(1002, "Invalid local message");
        });
    }
    QJsonArray localUrls() const {
        QJsonArray result; if (!server) return result;
        auto addresses = options.localAddresses;
        if (addresses.isEmpty()) {
            if (options.listenAddress.is_loopback()) addresses.append(QString::fromStdString(options.listenAddress.string()));
            else for (const auto &entry : ii::network::interfaces())
                if (!entry.loopback && entry.family == ii::network::AddressFamily::ipv4) addresses.append(QString::fromStdString(entry.address));
        }
        for (const auto &address : addresses) {
            QUrl url; url.setScheme(!hasIdentity(options.localTls) ? "ws" : "wss");
            url.setHost(address); url.setPort(server->serverPort()); result.append(url.toString());
        }
        return result;
    }
    void message(const QJsonObject &m) {
        const auto type = m.value("type").toString();
        if (type == "authenticated") {
            account = m.value("accountId").toString();
            send(&relay, {{"type", "register"}, {"peerId", options.peerId}, {"name", options.name},
                {"service", options.service}, {"host", options.hostFiles}, {"metadata", options.metadata}, {"localUrls", localUrls()},
                {"fingerprint", certificateFingerprint(options.localTls)}}); return;
        }
        if (type == "registered") { ready = true; backoff = 1000; error.clear(); emit q->stateChanged(); return; }
        if (type == "error") { error = m.value("error").toString(); fatal = true; emit q->stateChanged(); return; }
        if (!ready) return;
        if (type == "pairing") { emit q->pairingEvent(m); return; }
        if (type == "peers") {
            directory = m.value("peers").toArray();
            QSet<QString> active; for (const auto &entry : directory) active.insert(entry.toObject().value("peerId").toString());
            for (auto *s : links.keys()) if (!active.contains(links[s].peer) && links[s].outgoing) lost(s);
            emit q->peersChanged(); return;
        }
        if (type == "ticket") {
            if (tickets.size() >= 128) return;
            tickets.insert(m.value("ticket").toString(), {m.value("peerId").toString(), QDateTime::fromMSecsSinceEpoch(m.value("expires").toString().toLongLong(), QTimeZone::UTC)}); return;
        }
        if (type == "connect") {
            const auto peer = m.value("peerId").toString(); if (!connecting.contains(peer)) return;
            const auto info = m.value("descriptor").toObject(); QStringList urls;
            for (const auto &value : info.value("localUrls").toArray()) {
                const QUrl url(value.toString()); if (endpoint(url, "wss", "ws") && onLink(url)) urls.append(url.toString());
            }
            attempts.insert(peer, {urls, m.value("ticket").toString(), info.value("fingerprint").toString(),
                QDateTime::fromMSecsSinceEpoch(m.value("expires").toString().toLongLong(), QTimeZone::UTC)});
            nextLocal(peer); return;
        }
        if (type == "request") {
            const auto result = handle(m.value("peerId").toString(), m.value("payload").toObject());
            send(&relay, {{"type", "response"}, {"id", m.value("id")}, {"result", result}}); return;
        }
        const auto id = m.value("id").toString();
        if (type == "response" && requests.contains(id) && requests[id].transport == "remote") finish(id, m.value("result").toObject());
    }
    void sweep() {
        const auto now = QDateTime::currentDateTimeUtc();
        for (const auto &id : requests.keys()) if (requests[id].deadline <= now) finish(id, protocol::error("timeout"));
        for (const auto &ticket : tickets.keys()) if (tickets[ticket].expires <= now) tickets.remove(ticket);
        for (auto *s : links.keys()) {
            const auto l = links.value(s);
            if ((l.authenticated && l.expires <= now) || (!l.authenticated && l.deadline <= now)) lost(s);
        }
    }
};
Peer::Peer(QObject *parent) : QObject(parent), d(std::make_unique<Private>(this)) {}
Peer::~Peer() { stop(); }
bool Peer::start(const PeerOptions &options, RequestHandler handler) {
    stop(); d->error.clear();
    if (!endpoint(options.relayUrl, "wss", "ws") || options.credential.isEmpty() || options.credential.size() > 16384
        || !identifier(options.peerId) || !identifier(options.service) || options.name.size() > 128
        || options.localTimeoutMs < 50 || options.localTimeoutMs > 10000 || options.requestTimeoutMs < 100 || options.requestTimeoutMs > 120000) {
        d->error = "Invalid peer identity, credential, URL or timeout."; emit stateChanged(); return false;
    }
    d->options = options; d->handler = std::move(handler); d->fatal = false; d->backoff = 1000;
    if (options.localEnabled && options.localHostingEnabled && options.hostFiles) {
        d->server = listener(options.listenAddress, options.localPort, options.localTls, d->error);
        if (!d->server) { emit stateChanged(); return false; }
        connect(d->server.get(), &WebSocketServer::newConnection, this, [this] {
            while (d->server->hasPendingConnections()) {
                auto *s = d->server->nextPendingConnection(); s->setParent(this);
                if (d->links.size() >= 64) { s->close(); s->deleteLater(); continue; }
                Private::Link link; link.deadline = QDateTime::currentDateTimeUtc().addSecs(5);
                d->links.insert(s, link); d->attach(s);
            }
        });
    }
    d->running = true; d->timer.start(); d->openRelay(); return true;
}
void Peer::stop() {
    d->running = false; d->retry.stop(); d->authTimer.stop(); d->timer.stop();
    d->server.reset(); d->resetLocal();
    d->ready = false; d->relay.abort();
    for (const auto &id : d->requests.keys()) d->finish(id, protocol::error("stopped"));
    d->directory = {}; d->account.clear(); d->options.credential.clear(); d->handler = {};
    emit stateChanged(); emit peersChanged();
}
bool Peer::isReady() const { return d->ready; }
QJsonArray Peer::peers() const { return d->directory; }
QString Peer::accountId() const { return d->account; }
QString Peer::errorString() const { return d->error; }
quint16 Peer::localPort() const { return d->server ? d->server->serverPort() : 0; }
void Peer::refreshPeers() { if (d->ready) send(&d->relay, {{"type", "list"}}); }
QString Peer::request(const QString &peer, const QJsonObject &payload) {
    const auto id = uuid();
    if (d->requests.size() >= 32 || QJsonDocument(payload).toJson(QJsonDocument::Compact).size() > WireLimit - 1024) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, protocol::error("request_too_large_or_busy"), {}); }); return id;
    }
    d->requests.insert(id, {peer, payload, {}, {}, QDateTime::currentDateTimeUtc().addMSecs(d->options.requestTimeoutMs)});
    QTimer::singleShot(0, this, [this, id] { d->startRequest(id); }); return id;
}
QString Peer::createPairingOffer() {
    const auto id = uuid();
    if (d->ready) send(&d->relay, {{"type", "pair-offer"}, {"id", id}});
    else QTimer::singleShot(0, this, [this, id] { emit pairingEvent({{"id", id}, {"status", "error"}, {"error", "not_connected"}}); });
    return id;
}
QString Peer::claimPairingOffer(const QString &code, const QString &host) {
    const auto id = uuid();
    if (d->ready) send(&d->relay, {{"type", "pair-claim"}, {"id", id}, {"code", code}, {"host", host}});
    else QTimer::singleShot(0, this, [this, id] { emit pairingEvent({{"id", id}, {"status", "error"}, {"error", "not_connected"}}); });
    return id;
}
void Peer::confirmPairing(const QString &id) { if (d->ready) send(&d->relay, {{"type", "pair-confirm"}, {"pairingId", id}}); }
void Peer::cancelPairing(const QString &id) { if (d->ready && !id.isEmpty()) send(&d->relay, {{"type", "pair-cancel"}, {"id", id}}); }
}

#include "NetworkTransport.h"
#include <iiNetwork/WebSocket.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <deque>

namespace iiWebServer {
namespace net = ii::network;
namespace {
QString hex(const std::array<unsigned char, 32> &value) {
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(value.data()), value.size()).toHex());
}
struct Runtime final : QObject, std::enable_shared_from_this<Runtime> {
    ii::runtime::EventLoop loop{8192};
    QTimer timer;
    Runtime() {
        timer.setTimerType(Qt::PreciseTimer); timer.setInterval(2);
        QObject::connect(&timer, &QTimer::timeout, this, [this] {
            const auto keep = shared_from_this(); loop.poll();
        });
        timer.start();
    }
};
net::WebSocketLimits wireLimits() {
    net::WebSocketLimits limits;
    limits.message_bytes = 1024 * 1024;
    limits.buffered_bytes = 2 * 1024 * 1024;
    limits.output_bytes = 4 * 1024 * 1024;
    return limits;
}
void detach(net::ByteStream &stream) {
    stream.connected = {}; stream.readable = {}; stream.disconnected = {};
    stream.failed = {}; stream.drained = {}; stream.read_closed = {}; stream.bytes_written = {};
}
}
bool hasIdentity(const TlsConfiguration &tls) {
    const auto &id = tls.policy.identity;
    return (!id.certificate_chain_file.empty() && !id.private_key_file.empty()) ||
        (!id.certificate_chain_pem.empty() && !id.private_key_pem.empty());
}
QString certificateFingerprint(const TlsConfiguration &tls) {
    const auto &id = tls.policy.identity;
    auto info = id.certificate_chain_pem.empty() ? net::inspect_certificate_file(id.certificate_chain_file)
                                              : net::inspect_certificate(id.certificate_chain_pem);
    return info ? hex(info->certificate_sha256) : QString();
}
struct WebSocket::Private : std::enable_shared_from_this<WebSocket::Private> {
    WebSocket *owner;
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    std::unique_ptr<net::ByteStream> stream;
    std::optional<net::WebSocketSession> session;
    TlsConfiguration tls;
    QTimer deadline;
    QString error;
    QUrl url;
    bool server = false, notified = false, transportClosing = false;
    net::SocketState state = net::SocketState::closed;
    std::string output;
    std::vector<net::IpEndpoint> addresses;
    std::size_t nextAddress = 0;
    std::uint64_t generation = 0;
    explicit Private(WebSocket *q) : owner(q) { deadline.setSingleShot(true); }
    ~Private() { if (stream) { detach(*stream); stream->close(); } }
    void finish(QString message = {}) {
        if (state == net::SocketState::closed || state == net::SocketState::failed) return;
        error = std::move(message); state = error.isEmpty() ? net::SocketState::closed : net::SocketState::failed;
        ++generation;
        deadline.stop(); if (stream) { detach(*stream); stream->close(); }
        session.reset(); output.clear();
        const auto keep = shared_from_this();
        if (owner && !error.isEmpty()) emit owner->errorOccurred();
        if (owner && !notified) { notified = true; emit owner->disconnected(); }
    }
    void flush() {
        if (!session || !stream || !stream->is_connected()) return;
        output += session->take_output();
        if (output.size() + stream->pending_bytes() > 5 * 1024 * 1024) { finish("WebSocket output capacity exceeded"); return; }
        if (!output.empty() && stream->write(output)) output.clear();
        // A second graceful-close call would abort an already closing stream
        // and discard the response queued immediately before the close frame.
        if (state == net::SocketState::closing && output.empty() && !transportClosing) {
            transportClosing = true; stream->close(true);
        }
    }
    void opened() {
        if (!owner || state != net::SocketState::connecting) return;
        state = net::SocketState::connected; deadline.stop(); emit owner->connected();
    }
    void receive(std::string bytes) {
        if (!session || !owner) return;
        auto events = session->receive(bytes);
        if (!events) { finish(QString::fromStdString(events.error().message)); return; }
        for (const auto &event : *events) {
            if (!owner || !session) return;
            if (event.kind == net::WebSocketEventKind::upgrade_request) {
                auto accepted = session->accept();
                if (!accepted) { finish(QString::fromStdString(accepted.error().message)); return; }
                flush(); opened();
                // A peer may send its first frame in the same TCP read.
                if (session) receive({});
            } else if (event.kind == net::WebSocketEventKind::opened) opened();
            else if (event.kind == net::WebSocketEventKind::text) emit owner->textMessageReceived(QString::fromUtf8(event.payload));
            else if (event.kind == net::WebSocketEventKind::binary) emit owner->binaryMessageReceived(QByteArray(event.payload.data(), event.payload.size()));
            else if (event.kind == net::WebSocketEventKind::closed) {
                state = net::SocketState::closing; flush();
                if (owner) deadline.start(1000);
            }
        }
        flush();
    }
    void dial(std::vector<net::IpEndpoint> endpoints) {
        addresses = std::move(endpoints); nextAddress = 0; dialNext();
    }
    void retry(QString message) {
        if (state != net::SocketState::connecting || nextAddress == addresses.size()) { finish(std::move(message)); return; }
        ++generation;
        if (stream) { detach(*stream); stream->close(); }
        const auto weak = weak_from_this();
        QTimer::singleShot(0, owner, [weak] {
            if (auto p = weak.lock(); p && p->owner && p->state == net::SocketState::connecting) p->dialNext();
        });
    }
    void dialNext() {
        const auto p = shared_from_this(); const auto &url = p->url;
        ++generation;
        if (stream) { detach(*stream); stream->close(); stream.reset(); }
        session.reset(); output.clear();
        if (nextAddress == addresses.size()) { finish("No reachable WebSocket endpoint"); return; }
        const auto endpoint = addresses[nextAddress++];
        net::WebSocketClient request; request.host = url.authority(QUrl::FullyEncoded).toStdString();
        request.target = url.path(QUrl::FullyEncoded).isEmpty() ? "/" : url.path(QUrl::FullyEncoded).toStdString();
        if (url.hasQuery()) request.target += '?' + url.query(QUrl::FullyEncoded).toStdString();
        auto session = net::WebSocketSession::client(std::move(request), wireLimits());
        if (!session) { p->finish(QString::fromStdString(session.error().message)); return; }
        p->session.emplace(std::move(*session));
        if (url.scheme() == "wss") {
            auto context = net::TlsContext::client(p->tls);
            if (!context) { p->finish(QString::fromStdString(context.error().message)); return; }
            net::TlsStreamOptions bounds; bounds.receive_bytes = 2 * 1024 * 1024; bounds.pending_bytes = 5 * 1024 * 1024;
            auto socket = std::make_unique<net::TlsStream>(p->runtime->loop, *context, bounds);
            auto result = socket->connect(endpoint, url.host().toStdString());
            p->stream = std::move(socket);
            if (!result) { p->retry(QString::fromStdString(result.error().message)); return; }
        } else {
            auto socket = std::make_unique<net::Tcp>(p->runtime->loop);
            net::TcpOptions bounds; bounds.stream.receive_bytes = 2 * 1024 * 1024; bounds.stream.pending_bytes = 5 * 1024 * 1024;
            auto result = socket->connect(endpoint, bounds); p->stream = std::move(socket);
            if (!result) { p->retry(QString::fromStdString(result.error().message)); return; }
        }
        p->attach();
    }
    void attach() {
        const auto weak = weak_from_this();
        const auto current = generation;
        stream->connected = [weak, current] { if (auto p = weak.lock(); p && p->generation == current) p->flush(); };
        stream->readable = [weak, current] { if (auto p = weak.lock(); p && p->generation == current) p->receive(p->stream->read()); };
        stream->drained = [weak, current] { if (auto p = weak.lock(); p && p->generation == current) p->flush(); };
        stream->failed = [weak, current](std::string text) { if (auto p = weak.lock(); p && p->generation == current) p->retry(QString::fromStdString(text)); };
        stream->disconnected = [weak, current] {
            if (auto p = weak.lock(); p && p->generation == current) {
                if (p->state == net::SocketState::connecting) p->retry("WebSocket endpoint disconnected during handshake");
                else p->finish();
            }
        };
        stream->read_closed = stream->disconnected;
        const auto remaining = deadline.isActive() ? deadline.remainingTime() : 5000;
        deadline.disconnect();
        QObject::connect(&deadline, &QTimer::timeout, owner, [weak] {
            if (auto p = weak.lock()) p->finish(p->state == net::SocketState::closing ? QString() : QString("WebSocket handshake timeout"));
        });
        deadline.start(qMax(1, remaining));
        if (stream->is_connected()) flush();
    }
};
WebSocket::WebSocket(QObject *parent) : QObject(parent), d(std::make_shared<Private>(this)) {}
WebSocket::~WebSocket() { d->owner = nullptr; d->finish(); }
void WebSocket::setTlsConfiguration(TlsConfiguration tls) { d->tls = std::move(tls); }
void WebSocket::open(const QUrl &url) {
    const QPointer<WebSocket> alive(this);
    abort();
    if (!alive) return;
    const auto config = d->tls;
    d = std::make_shared<Private>(this); d->tls = config; d->url = url;
    d->state = net::SocketState::connecting;
    const auto weak = std::weak_ptr<Private>(d);
    QObject::connect(&d->deadline, &QTimer::timeout, this, [weak] {
        if (auto p = weak.lock()) p->finish("WebSocket resolution timeout");
    });
    d->deadline.start(5000);
    QTimer::singleShot(0, this, [weak] {
        const auto p = weak.lock(); if (!p || !p->owner || p->state != net::SocketState::connecting) return;
        const auto &url = p->url;
        if (!url.isValid() || (url.scheme() != "ws" && url.scheme() != "wss") || url.host().isEmpty() ||
            !url.userInfo().isEmpty() || url.hasFragment()) { p->finish("Invalid WebSocket URL"); return; }
        const auto port = url.port(url.scheme() == "wss" ? 443 : 80);
        if (port < 1 || port > 65535) { p->finish("Invalid WebSocket port"); return; }
        if (auto address = net::IpAddress::parse(url.host().toStdString())) { p->dial({{*address, quint16(port)}}); return; }
        static ii::runtime::Runtime resolver(ii::runtime::RuntimeOptions{2, 128});
        const auto sender = p->runtime->loop.sender();
        auto admission = resolver.post([weak, sender, host = url.host().toStdString(), port] {
            auto endpoints = net::resolve_host(host, quint16(port));
            const auto delivery = sender.post([weak, endpoints = std::move(endpoints)]() mutable {
                const auto p = weak.lock(); if (!p || !p->owner || p->state != net::SocketState::connecting) return;
                if (!endpoints || endpoints->empty()) { p->finish("Cannot resolve WebSocket endpoint"); return; }
                p->dial(std::move(*endpoints));
            });
            (void)delivery; // The bounded handshake deadline also covers a full delivery queue.
        });
        if (!admission) p->finish("WebSocket resolver is busy");
    });
}
void WebSocket::abort() { auto p = d; p->finish(); }
void WebSocket::close(quint16 code, const QString &reason) {
    auto p = d;
    if (!p->session || p->state != net::SocketState::connected) { p->finish(); return; }
    auto result = p->session->close(code, reason.toStdString());
    if (!result) { p->finish(QString::fromStdString(result.error().message)); return; }
    p->state = net::SocketState::closing; p->flush(); p->deadline.start(1000);
}
qint64 WebSocket::sendTextMessage(const QString &text) {
    auto p = d; if (!p->session || p->state != net::SocketState::connected) return -1;
    const auto bytes = text.toUtf8();
    if (bytesToWrite() + bytes.size() > 4 * 1024 * 1024) { p->finish("WebSocket output capacity exceeded"); return -1; }
    auto result = p->session->send_text(std::string_view(bytes.constData(), bytes.size()));
    if (!result) { p->finish(QString::fromStdString(result.error().message)); return -1; }
    p->flush(); return bytes.size();
}
qint64 WebSocket::sendBinaryMessage(const QByteArray &bytes) {
    auto p = d; if (!p->session || p->state != net::SocketState::connected) return -1;
    if (bytesToWrite() + bytes.size() > 4 * 1024 * 1024) { p->finish("WebSocket output capacity exceeded"); return -1; }
    auto result = p->session->send_binary(std::string_view(bytes.constData(), bytes.size()));
    if (!result) { p->finish(QString::fromStdString(result.error().message)); return -1; }
    p->flush(); return bytes.size();
}
void WebSocket::ping() {
    auto p = d; if (!p->session || p->state != net::SocketState::connected) return;
    const auto result = p->session->ping();
    if (!result) { p->finish(QString::fromStdString(result.error().message)); return; }
    p->flush();
}
net::SocketState WebSocket::state() const { return d->state; }
qint64 WebSocket::bytesToWrite() const { return d->output.size() + (d->stream ? d->stream->pending_bytes() : 0); }
QString WebSocket::errorString() const { return d->error; }
QString WebSocket::peerAddress() const {
    const auto endpoint = d->stream ? d->stream->peer_endpoint() : std::nullopt;
    return endpoint ? QString::fromStdString(endpoint->address.string()) : QString();
}
QString WebSocket::peerCertificateFingerprint() const {
    const auto *tls = dynamic_cast<net::TlsStream *>(d->stream.get());
    const auto info = tls ? tls->connection_info() : std::nullopt;
    return info && info->peer_certificate ? hex(info->peer_certificate->certificate_sha256) : QString();
}
struct WebSocketServer::Private {
    WebSocketServer *owner;
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    std::unique_ptr<net::Listener> plain;
    std::unique_ptr<net::TlsListener> secure;
    std::deque<QPointer<WebSocket>> pending;
    QList<QPointer<WebSocket>> handshakes;
    QString error;
    explicit Private(WebSocketServer *q) : owner(q) {}
    void accept(std::unique_ptr<net::ByteStream> stream) {
        handshakes.removeIf([](const auto &s) { return s.isNull(); });
        if (handshakes.size() + qsizetype(pending.size()) >= 64) { stream->close(); return; }
        auto *socket = new WebSocket(owner); auto p = socket->d;
        p->runtime = runtime; p->server = true; p->state = net::SocketState::connecting;
        auto session = net::WebSocketSession::server(wireLimits());
        if (!session) { delete socket; return; }
        p->session.emplace(std::move(*session)); p->stream = std::move(stream);
        handshakes.append(socket);
        QObject::connect(socket, &WebSocket::connected, owner, [this, socket] {
            handshakes.removeAll(socket); pending.push_back(socket); emit owner->newConnection();
        });
        QObject::connect(socket, &WebSocket::disconnected, owner, [this, socket] {
            handshakes.removeAll(socket);
            std::erase(pending, QPointer<WebSocket>(socket));
            if (socket->parent() == owner) socket->deleteLater();
        });
        p->attach();
    }
};
WebSocketServer::WebSocketServer(QObject *parent) : QObject(parent), d(std::make_unique<Private>(this)) {}
WebSocketServer::~WebSocketServer() {
    // QObject destroys children after members. Retire sockets while d is alive.
    for (auto *socket : findChildren<WebSocket *>(Qt::FindDirectChildrenOnly)) {
        socket->disconnect(this); socket->abort(); delete socket;
    }
}
bool WebSocketServer::listen(const Address &address, quint16 port, const TlsConfiguration &tls) {
    if (hasIdentity(tls)) {
        auto context = net::TlsContext::server(tls);
        if (!context) { d->error = QString::fromStdString(context.error().message); return false; }
        net::TlsStreamOptions bounds; bounds.receive_bytes = 2 * 1024 * 1024; bounds.pending_bytes = 5 * 1024 * 1024;
        d->secure = std::make_unique<net::TlsListener>(d->runtime->loop, *context, bounds);
        d->secure->accepted = [this](auto stream) { d->accept(std::move(stream)); };
        auto result = d->secure->listen({address, port});
        if (!result) { d->error = QString::fromStdString(result.error().message); return false; }
    } else {
        if (!address.is_loopback()) { d->error = "TLS is required for non-loopback listeners"; return false; }
        d->plain = std::make_unique<net::Listener>(d->runtime->loop);
        d->plain->accepted = [this](auto stream) { d->accept(std::move(stream)); };
        net::ListenerOptions bounds; bounds.accepted_socket.stream.receive_bytes = 2 * 1024 * 1024;
        bounds.accepted_socket.stream.pending_bytes = 5 * 1024 * 1024;
        auto result = d->plain->listen({address, port}, bounds);
        if (!result) { d->error = QString::fromStdString(result.error().message); return false; }
    }
    return true;
}
quint16 WebSocketServer::serverPort() const { return d->secure ? d->secure->port() : d->plain ? d->plain->port() : 0; }
bool WebSocketServer::hasPendingConnections() const { return !d->pending.empty(); }
WebSocket *WebSocketServer::nextPendingConnection() {
    while (!d->pending.empty()) { const auto value = d->pending.front(); d->pending.pop_front(); if (value) return value; }
    return nullptr;
}
QString WebSocketServer::errorString() const { return d->error; }
}

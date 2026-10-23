#pragma once
#include <NetworkTransport.h>
#include <QPointer>
#include <QTimer>
#include <deque>

// Raw TCP only for HTTP fault injection. Production framing remains iiNetwork's.
class TestTcpSocket final : public QObject {
    Q_OBJECT
public:
    TestTcpSocket(std::unique_ptr<ii::network::Tcp> value, QObject *parent)
        : QObject(parent), stream(std::move(value)) {
        stream->readable = [this] { emit readyRead(); };
        stream->disconnected = [this] { emit disconnected(); };
    }
    ~TestTcpSocket() override { stream->readable = {}; stream->disconnected = {}; stream->close(); }
    QByteArray readAll() { const auto bytes = stream->read(); return QByteArray(bytes.data(), bytes.size()); }
    void write(const QByteArray &bytes) { stream->write(std::string_view(bytes.constData(), bytes.size())); }
    void disconnectFromHost() { stream->close(true); }
signals:
    void readyRead();
    void disconnected();
private:
    std::unique_ptr<ii::network::Tcp> stream;
};
class TestTcpServer final : public QObject {
    Q_OBJECT
public:
    TestTcpServer() : listener(loop) {
        timer.setInterval(2); QObject::connect(&timer, &QTimer::timeout, this, [this] { loop.poll(); }); timer.start();
        listener.accepted = [this](auto stream) { pending.push_back(new TestTcpSocket(std::move(stream), this)); emit newConnection(); };
    }
    ~TestTcpServer() override {
        for (auto *child : findChildren<TestTcpSocket *>(Qt::FindDirectChildrenOnly)) delete child;
    }
    bool listen(const iiWebServer::Address &address) { return bool(listener.listen(ii::network::IpEndpoint{address, 0})); }
    quint16 serverPort() const { return listener.port(); }
    TestTcpSocket *nextPendingConnection() { const auto value = pending.front(); pending.pop_front(); return value; }
signals:
    void newConnection();
private:
    ii::runtime::EventLoop loop;
    ii::network::Listener listener;
    QTimer timer;
    std::deque<TestTcpSocket *> pending;
};

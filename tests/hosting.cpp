#include <iiServerHost.h>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslKey>
#include <QSslSocket>

using namespace iiServerHost;
namespace {
Authenticator accounts() {
    return [](const QByteArray &token, AuthCompletion done) {
        done({token.startsWith("alice") ? "alice" : token == "bob" ? "bob" : QString(),
              QDateTime::currentDateTimeUtc().addSecs(60)});
    };
}
PeerOptions options(quint16 port, const QString &id, const QByteArray &token = "alice") {
    PeerOptions o;
    o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(port));
    o.credential = token; o.peerId = id; o.name = id;
    o.listenAddress = QHostAddress::LocalHost;
    o.localAddresses = {"127.0.0.1"};
    o.localTimeoutMs = 150; o.requestTimeoutMs = 2000;
    return o;
}
QJsonObject reply(Peer &client, const QString &target, const QJsonObject &payload, QString *route = nullptr) {
    QSignalSpy done(&client, &Peer::completed);
    const auto id = client.request(target, payload);
    QElapsedTimer time; time.start();
    while (time.elapsed() < 5000) {
        for (const auto &event : done) if (event[0].toString() == id) {
            if (route) *route = event[2].toString();
            return event[1].toJsonObject();
        }
        QTest::qWait(5);
    }
    return {{"error", "test_timeout"}};
}
}

class HostingTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1"); }
    void localFirstAndSameAccountOnly() {
        RelayServer relay(accounts()); QVERIFY(relay.listen(QHostAddress::LocalHost));
        Peer host, client, stranger;
        QVERIFY(host.start(options(relay.port(), "host"), [](const auto &sender, const auto &p) {
            return QJsonObject{{"ok", true}, {"sender", sender}, {"echo", p}};
        }));
        QVERIFY(client.start(options(relay.port(), "client")));
        QVERIFY(stranger.start(options(relay.port(), "stranger", "bob")));
        QTRY_VERIFY(host.isReady() && client.isReady() && stranger.isReady());
        QTRY_COMPARE(client.peers().size(), 1);
        QCOMPARE(stranger.peers().size(), 0);
        QString route;
        auto result = reply(client, "host", {{"value", "nearby"}}, &route);
        QCOMPARE(result.value("sender").toString(), "client");
        QCOMPARE(result.value("echo").toObject().value("value").toString(), "nearby");
        QCOMPARE(route, "local");
        QVERIFY(!reply(stranger, "host", {}).value("ok").toBool());
    }
    void remoteRelayAndUnreachableLanFallback() {
        RelayServer relay(accounts()); QVERIFY(relay.listen(QHostAddress::LocalHost));
        for (bool local : {false, true}) {
            Peer host, client;
            auto o = options(relay.port(), "host"); o.localEnabled = local;
            // A deliberately unavailable loopback address makes the local path fail.
            o.localAddresses = {"127.0.0.2"};
            QVERIFY(host.start(o, [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"data", "remote"}}; }));
            QVERIFY(client.start(options(relay.port(), "client")));
            QTRY_VERIFY(host.isReady() && client.isReady());
            QTRY_COMPARE(client.peers().size(), 1);
            QString route;
            QCOMPARE(reply(client, "host", {}, &route).value("data").toString(), "remote");
            QCOMPARE(route, "remote");
        }
    }
    void rejectsCredentialsAndUnsafeTransport() {
        RelayServer relay(accounts()); QVERIFY(relay.listen(QHostAddress::LocalHost));
        Peer p;
        auto o = options(relay.port(), "invalid", "invalid");
        QVERIFY(p.start(o));
        QTRY_VERIFY(!p.errorString().isEmpty());
        QVERIFY(!p.isReady());
        o.relayUrl = QUrl("ws://192.168.1.1:5000");
        QVERIFY(!p.start(o));
        RelayServer unsafe(accounts());
        QVERIFY(!unsafe.listen(QHostAddress::AnyIPv4));
        o = options(relay.port(), "unsafe"); o.listenAddress = QHostAddress::AnyIPv4;
        QVERIFY(!p.start(o));
    }
    void expiredAuthorityAndStoppedHostDisappear() {
        bool valid = true;
        RelayServer relay([&](const auto &, AuthCompletion done) {
            done({valid ? "alice" : QString(), QDateTime::currentDateTimeUtc().addMSecs(800)});
        });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        Peer host, client;
        QVERIFY(host.start(options(relay.port(), "host")));
        QVERIFY(client.start(options(relay.port(), "client")));
        QTRY_VERIFY(host.isReady() && client.isReady());
        QTRY_COMPARE(client.peers().size(), 1);
        host.stop();
        QTRY_COMPARE(client.peers().size(), 0);
        valid = false;
        QTRY_VERIFY_WITH_TIMEOUT(!client.isReady(), 2000);
    }
    void localListenerRejectsUnauthenticatedRequests() {
        RelayServer relay(accounts()); QVERIFY(relay.listen(QHostAddress::LocalHost));
        Peer host; int calls = 0;
        QVERIFY(host.start(options(relay.port(), "host"), [&](const auto &, const auto &) { ++calls; return QJsonObject{{"ok", true}}; }));
        QTRY_VERIFY(host.isReady());
        QWebSocket attack;
        connect(&attack, &QWebSocket::connected, &attack, [&] {
            attack.sendTextMessage(R"({"type":"request","id":"attack","payload":{"op":"read","path":"secret"}})");
        });
        attack.open(QUrl(QString("ws://127.0.0.1:%1").arg(host.localPort())));
        QTest::qWait(200);
        QCOMPARE(calls, 0);
        QCOMPARE(attack.state(), QAbstractSocket::UnconnectedState);
    }
    void filesAreBoundedAndConfined() {
        QTemporaryDir root(TEST_DIRECTORY "/share-XXXXXX"), outside(TEST_DIRECTORY "/outside-XXXXXX");
        QVERIFY(root.isValid() && outside.isValid());
        FileShare share(root.path());
        const QByteArray data(700000, 'x');
        QFile file(root.filePath("large.bin")); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size()); file.close();
        QByteArray received;
        while (received.size() < data.size()) {
            auto result = share.handle({{"op", "read"}, {"path", "large.bin"}, {"offset", QString::number(received.size())}});
            QVERIFY(result.value("ok").toBool());
            auto chunk = QByteArray::fromBase64(result.value("data").toString().toLatin1());
            QVERIFY(chunk.size() <= FileShare::ChunkBytes); QVERIFY(!chunk.isEmpty()); received += chunk;
        }
        QCOMPARE(received, data);
        const auto oldVersion = share.handle({{"op", "stat"}, {"path", "large.bin"}}).value("version");
        QVERIFY(file.open(QIODevice::Append)); file.write("changed"); file.close();
        QCOMPARE(share.handle({{"op", "read"}, {"path", "large.bin"}, {"version", oldVersion}}).value("error").toString(), "file_changed");
        for (const QString &path : {"../secret", "/etc/passwd", "./large.bin", "a//b", "a\\b", "a:b"})
            QVERIFY(!share.handle({{"op", "read"}, {"path", path}}).value("ok").toBool());
        QVERIFY(QFile::link(outside.path(), root.filePath("link")));
        QVERIFY(!share.handle({{"op", "list"}, {"path", "link"}}).value("ok").toBool());
        QVERIFY(share.handle({{"op", "write"}, {"path", "new.txt"}, {"data", "aGVsbG8="}}).value("ok").toBool());
        QVERIFY(!share.handle({{"op", "write"}, {"path", "new.txt"}, {"data", "Y2hhbmdlZA=="}}).value("ok").toBool());
        QCOMPARE(share.handle({{"op", "read"}, {"path", "new.txt"}}).value("data").toString(), "aGVsbG8=");
        bool available = false;
        FileShare guarded(root.path(), [&] { return available; });
        QVERIFY(!guarded.handle({{"op", "list"}}).value("ok").toBool());
        QVERIFY(!share.handle({{"op", "read"}, {"path", "large.bin"}, {"offset", "-1"}}).value("ok").toBool());
    }
    void sessionVerifierIsolatesCookiesAndRejectsRedirect() {
        QTcpServer http; QVERIFY(http.listen(QHostAddress::LocalHost));
        QList<QByteArray> seen;
        connect(&http, &QTcpServer::newConnection, &http, [&] {
            auto *s = http.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [&, s] {
                const auto bytes = s->readAll(); if (!bytes.contains("\r\n\r\n")) return;
                seen.append(bytes);
                if (bytes.toLower().contains("cookie: redirect")) {
                    s->write("HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    s->disconnectFromHost(); return;
                }
                const bool alice = bytes.toLower().contains("cookie: alice");
                QByteArray body = alice ? R"({"account":{"sub":"verified-alice"}})" : R"({"account":null})";
                s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nSet-Cookie: injected=secret\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                s->disconnectFromHost();
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        });
        SessionAuthenticator auth(QUrl(QString("http://127.0.0.1:%1/Account/Session").arg(http.serverPort())));
        QList<Principal> verified;
        auth.authenticate("alice", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 1);
        QCOMPARE(verified[0].accountId, "verified-alice");
        auth.authenticate("bob", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 2);
        QVERIFY(verified[1].accountId.isEmpty());
        QVERIFY(!seen[1].contains("injected"));
        auth.authenticate("redirect", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 3);
        QVERIFY(verified[2].accountId.isEmpty());
        QCOMPARE(seen.size(), 3);
    }
    void tlsLocalPinAndRelayTrust() {
        QVERIFY(QSslSocket::supportsSsl());
        const auto certificates = QSslCertificate::fromPath(TEST_DIRECTORY "/test-cert.pem");
        QVERIFY(!certificates.isEmpty());
        QFile key(TEST_DIRECTORY "/test-key.pem"); QVERIFY(key.open(QIODevice::ReadOnly));
        auto tls = QSslConfiguration::defaultConfiguration();
        tls.setLocalCertificateChain(certificates); tls.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        RelayServer relay(accounts()); QVERIFY(relay.listen(QHostAddress::LocalHost, 0, tls));
        auto o = options(relay.port(), "host"); o.relayUrl.setScheme("wss");
        Peer untrusted;
        QVERIFY(untrusted.start(o));
        QTRY_VERIFY(!untrusted.errorString().isEmpty()); QVERIFY(!untrusted.isReady()); untrusted.stop();
        o.relayTls.addCaCertificates(QSslCertificate::fromPath(TEST_DIRECTORY "/test-ca.pem")); o.localTls = tls;
        Peer host, client;
        QVERIFY(host.start(o, [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"tls", true}}; }));
        o.peerId = "client"; o.hostFiles = false;
        QVERIFY(client.start(o));
        QTRY_VERIFY2(host.isReady() && client.isReady(), qPrintable(host.errorString() + " / " + client.errorString()));
        QTRY_COMPARE(client.peers().size(), 1);
        QString route;
        QVERIFY(reply(client, "host", {}, &route).value("tls").toBool());
        QCOMPARE(route, "local");
    }
};
QTEST_GUILESS_MAIN(HostingTests)
#include "hosting.moc"

#include "NetworkFixture.h"
#include <QJsonDocument>
#include <iiFileProviderPresentation.h>
#include <iiWebServer.h>
#include <QFile>
#include <QJsonArray>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace iiWebServer;
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
    o.listenAddress = Address::loopback();
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
    void reopeningCanDestroyTheSocketFromDisconnect() {
        WebSocketServer server; QVERIFY(server.listen(Address::loopback(), 0, {}));
        auto *socket = new WebSocket; QPointer<WebSocket> alive(socket);
        const QUrl endpoint(QString("ws://127.0.0.1:%1").arg(server.serverPort()));
        socket->open(endpoint);
        QTRY_VERIFY(socket->state() == ii::network::SocketState::connected);
        connect(socket, &WebSocket::disconnected, this, [socket] { delete socket; });
        socket->open(endpoint);
        QVERIFY(alive.isNull());
    }
    void hostNameDialUsesReachableAddress() {
        WebSocketServer server; QVERIFY(server.listen(Address::loopback(), 0, {}));
        WebSocket socket; QSignalSpy connected(&socket, &WebSocket::connected);
        socket.open(QUrl(QString("ws://localhost:%1").arg(server.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(connected.size(), 1, 5000);
    }
    void cancelledQueuedOpenStaysClosed() {
        WebSocketServer server; QVERIFY(server.listen(Address::loopback(), 0, {}));
        WebSocket socket; QSignalSpy accepted(&server, &WebSocketServer::newConnection);
        QSignalSpy connected(&socket, &WebSocket::connected);
        socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        socket.abort(); QTest::qWait(100);
        QCOMPARE(socket.state(), ii::network::SocketState::closed);
        QCOMPARE(connected.size(), 0); QCOMPARE(accepted.size(), 0);
    }
    void privatePresentationsDoNotConsumePublicPagination() {
        QTemporaryDir root(TEST_DIRECTORY "/presentation-share-XXXXXX"); QVERIFY(root.isValid());
        for (int i = 0; i < 270; ++i) {
            QFile document(root.filePath(QString(".asset-%1.iisacc-preview").arg(i)));
            QVERIFY(document.open(QIODevice::WriteOnly)); document.write("private derived fixture");
        }
        for (const auto &name : {"public.txt", ".public-hidden"}) {
            QFile original(root.filePath(name)); QVERIFY(original.open(QIODevice::WriteOnly)); original.write("original");
        }
        FileShare share(root.path());
        const auto listed = share.handle({{"op", "list"}, {"path", ""}});
        QVERIFY(listed.value("ok").toBool()); QCOMPARE(listed.value("entries").toArray().size(), 2);
        QVERIFY(listed.value("nextCursor").toString().isEmpty());
        QVERIFY(!share.handle({{"op", "stat"}, {"path", ".asset-0.iisacc-preview"}}).value("ok").toBool());
        QVERIFY(!share.handle({{"op", "write"}, {"path", "public.txt.iisacc.json"}, {"data", ""}}).value("ok").toBool());
        QVERIFY(share.handle({{"op", "stat"}, {"path", "public.txt"}}).value("ok").toBool());
        QVERIFY(share.handle({{"op", "write"}, {"path", "uploaded.txt"}, {"data", "cHVibGlzaGVk"}}).value("ok").toBool());
        QVERIFY(iiFileProvider::native::Presentation::waitForIdle(std::chrono::seconds(15)));
        const auto uploaded = iiFileProvider::native::Presentation::load(std::filesystem::path(root.filePath("uploaded.txt").toStdU16String()));
        QVERIFY(uploaded); QCOMPARE(uploaded->details.size, std::uint64_t(9));
    }
    void localFirstAndSameAccountOnly() {
        RelayServer relay(accounts()); QVERIFY(relay.listen(Address::loopback()));
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
        RelayServer relay(accounts()); QVERIFY(relay.listen(Address::loopback()));
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
        RelayServer relay(accounts()); QVERIFY(relay.listen(Address::loopback()));
        Peer p;
        auto o = options(relay.port(), "invalid", "invalid");
        QVERIFY(p.start(o));
        QTRY_VERIFY(!p.errorString().isEmpty());
        QVERIFY(!p.isReady());
        o.relayUrl = QUrl("ws://192.168.1.1:5000");
        QVERIFY(!p.start(o));
        RelayServer unsafe(accounts());
        QVERIFY(!unsafe.listen(Address::any()));
        o = options(relay.port(), "unsafe"); o.listenAddress = Address::any();
        QVERIFY(!p.start(o));
    }
    void expiredAuthorityAndStoppedHostDisappear() {
        bool valid = true;
        RelayServer relay([&](const auto &, AuthCompletion done) {
            done({valid ? "alice" : QString(), QDateTime::currentDateTimeUtc().addMSecs(800)});
        });
        QVERIFY(relay.listen(Address::loopback()));
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
        RelayServer relay(accounts()); QVERIFY(relay.listen(Address::loopback()));
        Peer host; int calls = 0;
        QVERIFY(host.start(options(relay.port(), "host"), [&](const auto &, const auto &) { ++calls; return QJsonObject{{"ok", true}}; }));
        QTRY_VERIFY(host.isReady());
        WebSocket attack;
        connect(&attack, &WebSocket::connected, &attack, [&] {
            attack.sendTextMessage(R"({"type":"request","id":"attack","payload":{"op":"read","path":"secret"}})");
        });
        attack.open(QUrl(QString("ws://127.0.0.1:%1").arg(host.localPort())));
        QTest::qWait(200);
        QCOMPARE(calls, 0);
        QVERIFY(attack.state() == ii::network::SocketState::closed || attack.state() == ii::network::SocketState::failed);
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
        QVERIFY(share.handle({{"op", "mkdir"}, {"path", QString::fromUtf8("자료")}}).value("ok").toBool());
        QVERIFY(share.handle({{"op", "write"}, {"path", QString::fromUtf8("자료/내용.txt")}, {"data", "aGVsbG8="}}).value("ok").toBool());
        QCOMPARE(share.handle({{"op", "read"}, {"path", QString::fromUtf8("자료/내용.txt")}}).value("data").toString(), "aGVsbG8=");
        const auto listed = share.handle({{"op", "list"}, {"path", QString::fromUtf8("자료")}});
        QVERIFY(listed.value("ok").toBool()); QCOMPARE(listed.value("entries").toArray().size(), 1);
        QFile lease(root.filePath(".iisacc-temporary.lock")); QVERIFY(lease.open(QIODevice::WriteOnly)); lease.close();
        QFile nestedLease(root.filePath(QString::fromUtf8("자료/.iisacc-temporary.lock"))); QVERIFY(nestedLease.open(QIODevice::WriteOnly)); nestedLease.close();
        for (const auto &entry : share.handle({{"op", "list"}}).value("entries").toArray())
            QVERIFY(entry.toObject().value("name") != ".iisacc-temporary.lock");
        QCOMPARE(share.handle({{"op", "list"}, {"path", QString::fromUtf8("자료")}}).value("entries").toArray().size(), 1);
        for (const auto &reserved : {QString(".iisacc-temporary.lock"), QString::fromUtf8("자료/.iisacc-temporary.lock")})
            for (const auto &operation : {"read", "stat", "write", "mkdir", "remove"})
                QVERIFY(!share.handle({{"op", operation}, {"path", reserved}, {"data", "aGVsbG8="}}).value("ok").toBool());
        QVERIFY(QFileInfo::exists(lease.fileName()) && QFileInfo(lease.fileName()).size() == 0);
        QVERIFY(QFileInfo::exists(nestedLease.fileName()) && QFileInfo(nestedLease.fileName()).size() == 0);
        bool available = false;
        FileShare guarded(root.path(), [&] { return available; });
        QVERIFY(!guarded.handle({{"op", "list"}}).value("ok").toBool());
        QVERIFY(!share.handle({{"op", "read"}, {"path", "large.bin"}, {"offset", "-1"}}).value("ok").toBool());
    }
    void sessionVerifierIsolatesCookiesAndRejectsRedirect() {
        TestTcpServer http; QVERIFY(http.listen(Address::loopback()));
        QList<QByteArray> seen;
        connect(&http, &TestTcpServer::newConnection, &http, [&] {
            auto *s = http.nextPendingConnection();
            connect(s, &TestTcpSocket::readyRead, s, [&, s] {
                const auto bytes = s->readAll(); if (!bytes.contains("\r\n\r\n")) return;
                seen.append(bytes);
                if (bytes.toLower().contains("cookie: redirect")) {
                    s->write("HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    s->disconnectFromHost(); return;
                }
                const bool alice = bytes.toLower().contains("cookie: alice");
                QByteArray body = alice ? R"({"data":{"accountSession":{"account":{"sub":"verified-alice"}}}})" : R"({"data":{"accountSession":{"account":null}}})";
                if (bytes.toLower().contains("cookie: partial")) body = R"({"data":{"accountSession":{"account":{"sub":"forged"}}},"errors":[{"message":"Failed"}]})";
                s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nSet-Cookie: injected=secret\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                s->disconnectFromHost();
            });
            connect(s, &TestTcpSocket::disconnected, s, &QObject::deleteLater);
        });
        SessionAuthenticator auth(QUrl(QString("http://127.0.0.1:%1/Account/GraphQL").arg(http.serverPort())));
        QList<Principal> verified;
        auth.authenticate("alice", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 1);
        QCOMPARE(verified[0].accountId, "verified-alice");
        QVERIFY(seen[0].startsWith("POST /Account/GraphQL "));
        const auto input = QJsonDocument::fromJson(seen[0].mid(seen[0].indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(input.value("query").toString(), QString("query AccountSession { accountSession { account { sub } } }"));
        auth.authenticate("bob", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 2);
        QVERIFY(verified[1].accountId.isEmpty());
        QVERIFY(!seen[1].contains("injected"));
        auth.authenticate("redirect", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 3);
        QVERIFY(verified[2].accountId.isEmpty());
        QCOMPARE(seen.size(), 3);
        auth.authenticate("partial", [&](Principal p) { verified.append(p); });
        QTRY_COMPARE(verified.size(), 4);
        QVERIFY(verified[3].accountId.isEmpty());
    }
    void tlsLocalPinAndRelayTrust() {
        TlsConfiguration tls;
        tls.policy.identity.certificate_chain_file = TEST_DIRECTORY "/test-cert.pem";
        tls.policy.identity.private_key_file = TEST_DIRECTORY "/test-key.pem";
        RelayServer relay(accounts()); QVERIFY(relay.listen(Address::loopback(), 0, tls));
        auto o = options(relay.port(), "host"); o.relayUrl.setScheme("wss");
        Peer untrusted;
        QVERIFY(untrusted.start(o));
        QTRY_VERIFY(!untrusted.errorString().isEmpty()); QVERIFY(!untrusted.isReady()); untrusted.stop();
        o.relayTls.policy.ca_file = TEST_DIRECTORY "/test-ca.pem"; o.localTls = tls;
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

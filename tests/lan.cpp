#include <iiServerHost.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include "../BinaryFrame.h"
#include <QWebSocket>
#include <QSslError>

using namespace iiServerHost;
class LanTests : public QObject {
    Q_OBJECT
private slots:
    void binaryFrameBoundsAndWireSavings() {
        const QByteArray data(256 * 1024, '\xff');
        for (const auto &message : {
            QJsonObject{{"type", "request"}, {"payload", QJsonObject{{"message", QJsonObject{{"data", QString::fromLatin1(data.toBase64())}}}}}},
            QJsonObject{{"type", "response"}, {"result", QJsonObject{{"result", QJsonObject{{"data", QString::fromLatin1(data.toBase64())}}}}}},
            QJsonObject{{"type", "response"}, {"result", QJsonObject{{"data", QString::fromLatin1(data.toBase64())}}}}
        }) {
            const auto frame = protocol::BinaryFrame::encode(message); QVERIFY(!frame.isEmpty());
            QCOMPARE(protocol::BinaryFrame::decode(frame), message);
            const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
            QVERIFY(frame.size() < json.size() * 0.76); QVERIFY(frame.size() < data.size() + 256);
            qInfo("binary frame=%lld json=%lld payload=%lld", qlonglong(frame.size()), qlonglong(json.size()), qlonglong(data.size()));
            auto malformed = frame; malformed[0] = 'X'; QVERIFY(protocol::BinaryFrame::decode(malformed).isEmpty());
            malformed = frame; malformed[4] = '\x7f'; QVERIFY(protocol::BinaryFrame::decode(malformed).isEmpty());
            QVERIFY(protocol::BinaryFrame::decode(frame.left(8)).isEmpty());
            QVERIFY(protocol::BinaryFrame::decode(frame + QByteArray(256 * 1024, 'x')).isEmpty());
        }
        QVERIFY(protocol::BinaryFrame::encode({{"data", "not a known file path"}}).isEmpty());
    }
    void negotiatedBinaryUploadAndDownload() {
        LanPeer host, client; const QByteArray bytes(256 * 1024, '\x81'); int chunks = 0;
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &, const auto &request) {
            if (request.value("op") == "list") return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
            const auto data = request.value("message").toObject().value("data").toString();
            if (QByteArray::fromBase64(data.toLatin1()) == bytes) ++chunks;
            return QJsonObject{{"ok", true}, {"result", QJsonObject{{"ok", true}, {"data", data}}}};
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(client.join(host.createOffer(), "phone", "Phone")); QTRY_VERIFY(client.connected()); QVERIFY(client.binaryTransferEnabled());
        QSignalSpy completed(&client, &LanPeer::completed);
        for (int i = 0; i < 4; ++i) QVERIFY(!client.request("desktop", {{"op", "society.sync"},
            {"message", QJsonObject{{"data", QString::fromLatin1(bytes.toBase64())}}}}).isEmpty());
        QTRY_COMPARE(completed.size(), 4); QCOMPARE(chunks, 4);
        for (const auto &result : completed) QCOMPARE(QByteArray::fromBase64(result[1].toJsonObject().value("result").toObject().value("data").toString().toLatin1()), bytes);
    }
    void binaryBeforePairingCannotReadFiles() {
        LanPeer host; int reads = 0;
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &, const auto &) {
            ++reads; return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        LanLink offer; QVERIFY(LanLink::decode(host.createOffer(), &offer));
        QWebSocket unpaired; QSignalSpy disconnected(&unpaired, &QWebSocket::disconnected);
        connect(&unpaired, &QWebSocket::sslErrors, &unpaired, [&](const QList<QSslError> &errors) { unpaired.ignoreSslErrors(errors); });
        connect(&unpaired, &QWebSocket::connected, &unpaired, [&] {
            const auto frame = protocol::BinaryFrame::encode({{"type", "request"}, {"id", "unpaired"},
                {"payload", QJsonObject{{"message", QJsonObject{{"data", "YWJj"}}}}}});
            QVERIFY(!frame.isEmpty()); unpaired.sendBinaryMessage(frame);
        });
        unpaired.open(QUrl("wss://127.0.0.1:" + QString::number(offer.port)));
        QTRY_COMPARE(disconnected.size(), 1); QCOMPARE(reads, 0); QVERIFY(host.pairedDeviceIds().isEmpty());
    }
    void oldJsonClientsRemainCompatible() {
        LanPeer host; const QByteArray bytes(8192, 'L');
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &, const auto &request) {
            return request.value("op") == "list" ? QJsonObject{{"ok", true}, {"entries", QJsonArray()}}
                : QJsonObject{{"ok", true}, {"data", QString::fromLatin1(bytes.toBase64())}};
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        LanLink offer; QVERIFY(LanLink::decode(host.createOffer(), &offer));
        QWebSocket old; QSignalSpy binary(&old, &QWebSocket::binaryMessageReceived); bool paired = false, received = false;
        const auto send = [&](const QJsonObject &value) { old.sendTextMessage(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))); };
        connect(&old, &QWebSocket::sslErrors, &old, [&](const QList<QSslError> &errors) { old.ignoreSslErrors(errors); });
        connect(&old, &QWebSocket::connected, &old, [&] { send({{"type", "pair"}, {"host", "desktop"}, {"code", offer.code}, {"peerId", "old"}, {"name", "Old"}}); });
        connect(&old, &QWebSocket::textMessageReceived, &old, [&](const QString &text) {
            const auto value = QJsonDocument::fromJson(text.toUtf8()).object();
            if (value.value("type") == "ready") send({{"type", "confirm"}});
            if (value.value("type") == "paired") { paired = true; send({{"type", "request"}, {"id", "request"}, {"payload", QJsonObject{{"op", "read"}}}}); }
            if (value.value("type") == "response") received = QByteArray::fromBase64(value.value("result").toObject().value("data").toString().toLatin1()) == bytes;
        });
        old.open(QUrl("wss://127.0.0.1:" + QString::number(offer.port)));
        QTRY_VERIFY(paired && received); QCOMPARE(binary.size(), 0);
    }
    void discoveredDeviceNeedsConfirmationBeforeFiles() {
        LanPeer host, client, wrong;
        int reads = 0;
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &, const auto &) {
            ++reads; return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        const auto offer = host.createDeviceOffer("phone"); QVERIFY(!offer.isEmpty());
        QVERIFY(wrong.join(offer, "other", "Other")); QTRY_COMPARE(wrong.phase(), "error");
        QCOMPARE(reads, 0);
        QVERIFY(client.join(offer, "phone", "Phone"));
        QTRY_COMPARE(host.phase(), "confirming"); QTRY_COMPARE(client.phase(), "confirming");
        QVERIFY(!client.connected()); QCOMPARE(reads, 0);
        QCOMPARE(host.verificationCode(), client.verificationCode());
        QCOMPARE(host.verificationCode().size(), 14);
        QVERIFY(host.confirmDevice()); QTRY_VERIFY(client.connected());
        QCOMPARE(host.pairedDeviceIds(), QStringList{"phone"});
        QCOMPARE(client.pairedDeviceIds(), QStringList{"desktop"});
        QCOMPARE(reads, 1); QVERIFY(!host.confirmDevice());
        QVERIFY(host.createDeviceOffer("desktop").isEmpty());
    }
    void discoveredDeviceCancellationClosesPendingConnection() {
        LanPeer host, client;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) {
            return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(client.join(host.createDeviceOffer("phone"), "phone", "Phone"));
        QTRY_COMPARE(host.phase(), "confirming"); host.cancelPairing();
        QTRY_COMPARE(client.phase(), "error"); QVERIFY(!client.connected());
        QVERIFY(host.verificationCode().isEmpty());
    }
    void strictLocalQr() {
        LanLink link{{"192.168.10.20", "10.0.0.8"}, 9443, "desktop", "Desktop", QString(64, 'a'), QString(64, 'b'), QDateTime::currentDateTimeUtc().addSecs(60)}, decoded;
        QVERIFY(LanLink::decode(link.encode(), &decoded));
        QCOMPARE(decoded.addresses, link.addresses); QCOMPARE(decoded.code, link.code);
        link.name = QString::fromUtf8("작업실 & 100% Desktop");
        QVERIFY(LanLink::decode(link.encode(), &decoded)); QCOMPARE(decoded.name, link.name);
        QVERIFY(!LanLink::decode(link.encode() + "&code=" + QString(64, 'c'), &decoded));
        QVERIFY(!LanLink::decode(link.encode() + "#fragment", &decoded));
        for (const auto &host : {"iisacc.com", "8.8.8.8", "0.0.0.0", "224.1.1.1", "169.254.169.254"}) {
            link.addresses = {host}; QVERIFY2(link.encode().isEmpty(), host);
        }
    }
    void noRelayPairingAndFiles() {
        QTemporaryDir root(TEST_DIRECTORY "/lan-XXXXXX");
        QFile file(root.filePath("hello.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("local bytes"); file.close();
        FileShare share(root.path()); LanPeer host, client, replay;
        QVERIFY(host.startHost("desktop", "My Desktop", [&](const auto &, const auto &r) { return share.handle(r); }, {"127.0.0.1"}, QHostAddress::LocalHost));
        const auto qr = host.createOffer(); QVERIFY(!qr.isEmpty());
        QSignalSpy h(&host, &LanPeer::paired), c(&client, &LanPeer::paired);
        QVERIFY(client.join(qr, "phone", "My iPhone"));
        QTRY_VERIFY(client.phase() == "paired" || client.phase() == "error");
        QVERIFY2(client.connected(), qPrintable(client.errorString()));
        QTRY_COMPARE(c.size(), 1); QTRY_COMPARE(h.size(), 1);
        QVERIFY(host.qrText().isEmpty()); QVERIFY(client.connected()); QCOMPARE(client.peerName(), "My Desktop");
        QSignalSpy response(&client, &LanPeer::completed);
        const auto id = client.request("desktop", {{"op", "read"}, {"path", "hello.txt"}, {"offset", "0"}});
        QTRY_COMPARE(response.size(), 1); QCOMPARE(response[0][0].toString(), id);
        const auto result = response[0][1].toJsonObject(); QVERIFY(result["ok"].toBool());
        QCOMPARE(QByteArray::fromBase64(result["data"].toString().toLatin1()), QByteArray("local bytes"));
        QCOMPARE(response[0][2].toString(), "local");
        QVERIFY(replay.join(qr, "replay", "Replay")); QTRY_COMPARE(replay.phase(), "error"); QVERIFY(!replay.connected());
        host.stop(); QTRY_VERIFY(!client.connected());
    }
    void certificatePinMismatchNeverPairs() {
        LanPeer host, client;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"entries", QJsonArray()}}; }, {"127.0.0.1"}, QHostAddress::LocalHost));
        LanLink link; QVERIFY(LanLink::decode(host.createOffer(), &link)); link.fingerprint = QString(64, '0');
        QSignalSpy paired(&host, &LanPeer::paired); QVERIFY(client.join(link.encode(), "phone", "Phone"));
        QTRY_COMPARE(client.phase(), "error"); QVERIFY(!client.connected()); QCOMPARE(paired.size(), 0);
        QVERIFY(client.errorString().contains("certificate"));
    }
    void cancellationExpiryAndFilesFailure() {
        LanPeer host, client;
        QVERIFY(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject{{"ok", false}, {"error", "container_unavailable"}}; }, {"127.0.0.1"}, QHostAddress::LocalHost));
        const auto cancelled = host.createOffer(); host.cancelPairing();
        QVERIFY(client.join(cancelled, "phone", "Phone")); QTRY_COMPARE(client.phase(), "error");
        auto qr = host.createOffer(1); QTRY_VERIFY(host.qrText().isEmpty());
        QVERIFY(!client.join(qr, "phone", "Phone"));
        qr = host.createOffer(); QSignalSpy paired(&host, &LanPeer::paired);
        QVERIFY(client.join(qr, "phone", "Phone")); QTRY_COMPARE(client.phase(), "error");
        QCOMPARE(paired.size(), 0); QVERIFY(!client.connected());
    }
};
QTEST_GUILESS_MAIN(LanTests)
#include "lan.moc"

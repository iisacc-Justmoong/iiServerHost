#include <iiServerHost.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace iiServerHost;
class LanTests : public QObject {
    Q_OBJECT
private slots:
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

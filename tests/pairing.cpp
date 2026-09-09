#include <iiServerHost.h>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace iiServerHost;
namespace {
PeerOptions options(quint16 port, QString id, QByteArray account = "alice", bool host = false) {
    PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(port));
    o.peerId = id; o.name = id; o.credential = account; o.hostFiles = host;
    o.localEnabled = false; o.requestTimeoutMs = 1000; return o;
}
QJsonObject pairEvent(QSignalSpy &spy, const QString &id, const QString &status) {
    for (const auto &entry : spy) {
        const auto value = entry[0].toJsonObject();
        if (value.value("id").toString() == id && value.value("status").toString() == status) return value;
    }
    return {};
}
}
class PairingTests : public QObject {
    Q_OBJECT
private slots:
    void qrRoundTripAndStrictParsing() {
        PairingLink link{QUrl("wss://iisacc.com/society/relay"), "desktop-1", QString(64, 'a')}, decoded;
        QVERIFY(PairingLink::decode(link.encode(), &decoded));
        QCOMPARE(decoded.relayUrl, link.relayUrl); QCOMPARE(decoded.hostId, link.hostId); QCOMPARE(decoded.code, link.code);
        const auto plainRelay = link.relayUrl;
        link.relayUrl = QUrl("wss://iisacc.com/relay%2Fsegment/%25encoded");
        QVERIFY(PairingLink::decode(link.encode(), &decoded));
        QCOMPARE(decoded.relayUrl, link.relayUrl);
        link.relayUrl = plainRelay;
        QVERIFY(!PairingLink::decode(link.encode() + "&code=" + QString(64, 'b'), &decoded));
        QVERIFY(!PairingLink::decode(link.encode() + "#extra", &decoded));
        QVERIFY(!PairingLink::decode("https://example.com", &decoded));
        QVERIFY(!PairingLink::decode(QString(3000, 'a'), &decoded));
        link.relayUrl = QUrl("ws://192.168.1.20:9443"); QVERIFY(link.encode().isEmpty());
        link.relayUrl = QUrl("wss://user:secret@iisacc.com"); QVERIFY(link.encode().isEmpty());
        link.relayUrl = QUrl("wss://iisacc.com?token=secret"); QVERIFY(link.encode().isEmpty());
        link.relayUrl = QUrl("ws://127.0.0.1:1234"); QVERIFY(!link.encode().isEmpty());
    }
    void oneUsePairingNeedsClientConfirmation() {
        RelayServer relay([](const auto &account, AuthCompletion done) { done({QString::fromUtf8(account), QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        Peer host, client, other, stranger;
        QVERIFY(host.start(options(relay.port(), "Desktop", "alice", true), [](const auto &, const auto &) { return QJsonObject{{"ok", true}}; }));
        QVERIFY(client.start(options(relay.port(), "iPhone")));
        QVERIFY(other.start(options(relay.port(), "other")));
        QVERIFY(stranger.start(options(relay.port(), "stranger", "bob")));
        QTRY_VERIFY(host.isReady() && client.isReady() && other.isReady() && stranger.isReady());
        QSignalSpy hostEvents(&host, &Peer::pairingEvent), clientEvents(&client, &Peer::pairingEvent),
            otherEvents(&other, &Peer::pairingEvent), strangerEvents(&stranger, &Peer::pairingEvent);
        const auto offer = host.createPairingOffer();
        QTRY_VERIFY(!pairEvent(hostEvents, offer, "offered").isEmpty());
        const auto code = pairEvent(hostEvents, offer, "offered").value("code").toString();
        QCOMPARE(code.size(), 64);
        const auto wrongAccount = stranger.claimPairingOffer(code, "Desktop");
        QTRY_VERIFY(!pairEvent(strangerEvents, wrongAccount, "error").isEmpty());
        const auto wrongHost = other.claimPairingOffer(code, "different-host");
        QTRY_VERIFY(!pairEvent(otherEvents, wrongHost, "error").isEmpty());
        const auto claim = client.claimPairingOffer(code, "Desktop");
        QTRY_VERIFY(!pairEvent(clientEvents, claim, "claimed").isEmpty());
        QTRY_VERIFY(!pairEvent(hostEvents, offer, "claimed").isEmpty());
        QVERIFY(pairEvent(clientEvents, claim, "paired").isEmpty());
        const auto pairId = pairEvent(clientEvents, claim, "claimed").value("pairingId").toString();
        const auto replay = other.claimPairingOffer(code, "Desktop");
        QTRY_VERIFY(!pairEvent(otherEvents, replay, "error").isEmpty());
        other.confirmPairing(pairId);
        QTest::qWait(30); QVERIFY(pairEvent(hostEvents, offer, "paired").isEmpty());
        client.confirmPairing(pairId);
        QTRY_VERIFY(!pairEvent(clientEvents, claim, "paired").isEmpty());
        QTRY_VERIFY(!pairEvent(hostEvents, offer, "paired").isEmpty());
        QCOMPARE(pairEvent(hostEvents, offer, "paired").value("peerId").toString(), QString("iPhone"));
    }
    void cancellationRotationAndHostDisconnectInvalidateOffers() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost)); Peer host, client;
        QVERIFY(host.start(options(relay.port(), "host", "alice", true)));
        QVERIFY(client.start(options(relay.port(), "client")));
        QTRY_VERIFY(host.isReady() && client.isReady());
        QSignalSpy h(&host, &Peer::pairingEvent), c(&client, &Peer::pairingEvent);
        auto first = host.createPairingOffer(); QTRY_VERIFY(!pairEvent(h, first, "offered").isEmpty());
        auto code = pairEvent(h, first, "offered").value("code").toString();
        auto second = host.createPairingOffer(); QTRY_VERIFY(!pairEvent(h, second, "offered").isEmpty());
        auto rejected = client.claimPairingOffer(code, "host"); QTRY_VERIFY(!pairEvent(c, rejected, "error").isEmpty());
        code = pairEvent(h, second, "offered").value("code").toString();
        host.cancelPairing(second); QTRY_VERIFY(!pairEvent(h, second, "cancelled").isEmpty());
        rejected = client.claimPairingOffer(code, "host"); QTRY_VERIFY(!pairEvent(c, rejected, "error").isEmpty());
        auto third = host.createPairingOffer(); QTRY_VERIFY(!pairEvent(h, third, "offered").isEmpty());
        code = pairEvent(h, third, "offered").value("code").toString();
        auto claim = client.claimPairingOffer(code, "host"); QTRY_VERIFY(!pairEvent(c, claim, "claimed").isEmpty());
        host.stop(); QTRY_VERIFY(!pairEvent(c, claim, "cancelled").isEmpty());
        QVERIFY(pairEvent(c, claim, "paired").isEmpty());
    }
    void clientsCannotOfferAndAttemptsAreLimited() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost)); Peer client;
        QVERIFY(client.start(options(relay.port(), "client"))); QTRY_VERIFY(client.isReady());
        QSignalSpy events(&client, &Peer::pairingEvent);
        const auto offer = client.createPairingOffer(); QTRY_VERIFY(!pairEvent(events, offer, "error").isEmpty());
        QString claim;
        for (int i = 0; i < 7; ++i) claim = client.claimPairingOffer(QString(64, 'a'), "host");
        QTRY_VERIFY(!pairEvent(events, claim, "error").isEmpty());
        QCOMPARE(pairEvent(events, claim, "error").value("error").toString(), QString("pairing_rate_limited"));
    }
    void expiredAuthenticationCannotCompletePairing() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addMSecs(800)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost)); Peer host, client;
        QVERIFY(host.start(options(relay.port(), "host", "alice", true)));
        QVERIFY(client.start(options(relay.port(), "client")));
        QTRY_VERIFY(host.isReady() && client.isReady());
        QSignalSpy h(&host, &Peer::pairingEvent), c(&client, &Peer::pairingEvent);
        auto offer = host.createPairingOffer(); QTRY_VERIFY(!pairEvent(h, offer, "offered").isEmpty());
        auto claim = client.claimPairingOffer(pairEvent(h, offer, "offered").value("code").toString(), "host");
        QTRY_VERIFY(!pairEvent(c, claim, "claimed").isEmpty());
        QTest::qWait(850);
        client.confirmPairing(pairEvent(c, claim, "claimed").value("pairingId").toString());
        QTest::qWait(80); QVERIFY(pairEvent(c, claim, "paired").isEmpty());
    }
};
QTEST_GUILESS_MAIN(PairingTests)
#include "pairing.moc"

#include <ServerHost.h>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSslKey>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("ii-server-host");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOptions({{"relay", "Trusted WSS relay URL.", "url"}, {"id", "Stable peer identifier.", "id"},
        {"credential-file", "Private file containing the account Cookie header.", "path"},
        {"root", "Absolute directory to share.", "path"}, {"address", "Local listen address.", "ip", "127.0.0.1"},
        {"certificate", "Local TLS certificate.", "path"}, {"key", "Local TLS private key.", "path"},
        {"no-local", "Use only the remote relay transport."},
        {"peer", "Request from this peer and exit.", "id"}, {"path", "Relative path to request.", "path", ""},
        {"read", "Read a chunk instead of listing a directory."}});
    parser.process(app);
    QFile credential(parser.value("credential-file"));
    if (!credential.open(QIODevice::ReadOnly) || credential.size() > 16384) parser.showHelp(2);
    iiServerHost::PeerOptions options;
    options.credential = credential.readAll().trimmed(); options.relayUrl = QUrl(parser.value("relay"));
    options.peerId = parser.value("id"); options.name = options.peerId;
    options.localEnabled = !parser.isSet("no-local"); options.listenAddress = QHostAddress(parser.value("address"));
    options.hostFiles = parser.isSet("root");
    if (parser.isSet("certificate") || parser.isSet("key")) {
        const auto certs = QSslCertificate::fromPath(parser.value("certificate")); QFile file(parser.value("key"));
        if (certs.isEmpty() || !file.open(QIODevice::ReadOnly)) parser.showHelp(2);
        const auto pem = file.readAll(); QSslKey key(pem, QSsl::Rsa); if (key.isNull()) key = QSslKey(pem, QSsl::Ec);
        if (key.isNull()) parser.showHelp(2);
        options.localTls = QSslConfiguration::defaultConfiguration(); options.localTls.setLocalCertificateChain(certs); options.localTls.setPrivateKey(key);
    }
    iiServerHost::FileShare share(parser.value("root")); iiServerHost::Peer peer;
    bool announced = false, requested = false;
    const auto request = [&] {
        if (!peer.isReady() || requested || !parser.isSet("peer")) return;
        bool found = false; for (const auto &entry : peer.peers()) if (entry.toObject().value("peerId") == parser.value("peer")) found = true;
        if (!found) return;
        requested = true; peer.request(parser.value("peer"), {{"op", parser.isSet("read") ? "read" : "list"}, {"path", parser.value("path")}});
    };
    QObject::connect(&peer, &iiServerHost::Peer::peersChanged, &app, request);
    QObject::connect(&peer, &iiServerHost::Peer::stateChanged, &app, [&] {
        if (peer.isReady() && !announced && !parser.isSet("peer")) {
            announced = true;
            QTextStream(stdout) << QJsonDocument(QJsonObject{{"ready", true}, {"localPort", peer.localPort()}}).toJson(QJsonDocument::Compact) << Qt::endl;
        }
        request();
    });
    QObject::connect(&peer, &iiServerHost::Peer::completed, &app, [&](const QString &, const QJsonObject &result, const QString &transport) {
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"result", result}, {"transport", transport}}).toJson(QJsonDocument::Compact) << Qt::endl;
        app.exit(result.value("ok").toBool() ? 0 : 1);
    });
    if (parser.isSet("peer")) QTimer::singleShot(8000, &app, [&] {
        QTextStream(stdout) << "{\"error\":\"host_unavailable_or_timeout\"}" << Qt::endl; app.exit(1);
    });
    if (!peer.start(options, [&share](const auto &, const auto &payload) { return share.handle(payload); })) {
        QTextStream(stderr) << peer.errorString() << Qt::endl; return 1;
    }
    return app.exec();
}

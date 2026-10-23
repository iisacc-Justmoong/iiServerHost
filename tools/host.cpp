#include <WebServer.h>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("ii-web-server");
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
    iiWebServer::PeerOptions options;
    options.credential = credential.readAll().trimmed(); options.relayUrl = QUrl(parser.value("relay"));
    options.peerId = parser.value("id"); options.name = options.peerId;
    options.localEnabled = !parser.isSet("no-local"); const auto bind = iiWebServer::Address::parse(parser.value("address").toStdString());
    if (!bind) parser.showHelp(2); options.listenAddress = *bind;
    options.hostFiles = parser.isSet("root");
    if (parser.isSet("certificate") || parser.isSet("key")) {
        options.localTls.policy.identity.certificate_chain_file = parser.value("certificate").toStdString();
        options.localTls.policy.identity.private_key_file = parser.value("key").toStdString();
    }
    iiWebServer::FileShare share(parser.value("root")); iiWebServer::Peer peer;
    bool announced = false, requested = false;
    const auto request = [&] {
        if (!peer.isReady() || requested || !parser.isSet("peer")) return;
        bool found = false; for (const auto &entry : peer.peers()) if (entry.toObject().value("peerId") == parser.value("peer")) found = true;
        if (!found) return;
        requested = true; peer.request(parser.value("peer"), {{"op", parser.isSet("read") ? "read" : "list"}, {"path", parser.value("path")}});
    };
    QObject::connect(&peer, &iiWebServer::Peer::peersChanged, &app, request);
    QObject::connect(&peer, &iiWebServer::Peer::stateChanged, &app, [&] {
        if (peer.isReady() && !announced && !parser.isSet("peer")) {
            announced = true;
            QTextStream(stdout) << QJsonDocument(QJsonObject{{"ready", true}, {"localPort", peer.localPort()}}).toJson(QJsonDocument::Compact) << Qt::endl;
        }
        request();
    });
    QObject::connect(&peer, &iiWebServer::Peer::completed, &app, [&](const QString &, const QJsonObject &result, const QString &transport) {
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

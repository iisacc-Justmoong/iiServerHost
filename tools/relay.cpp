#include <iiServerHost.h>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QSslKey>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("ii-server-relay"); app.setApplicationVersion("0.3.0");
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{{"a", "address"}, "Listen address; public addresses require TLS.", "address", "127.0.0.1"},
        {{"p", "port"}, "Listen port.", "port", "9443"},
        {"session-url", "Trusted account session verification endpoint.", "url", "https://iisacc.com/Account/Session"},
        {"certificate", "PEM TLS certificate chain.", "path"}, {"key", "PEM TLS private key.", "path"}});
    parser.process(app);
    QSslConfiguration tls;
    if (parser.isSet("certificate") || parser.isSet("key")) {
        const auto certificates = QSslCertificate::fromPath(parser.value("certificate"));
        QFile file(parser.value("key"));
        if (certificates.isEmpty() || !file.open(QIODevice::ReadOnly)) parser.showHelp(2);
        const auto pem = file.readAll(); QSslKey key(pem, QSsl::Rsa); if (key.isNull()) key = QSslKey(pem, QSsl::Ec);
        if (key.isNull()) parser.showHelp(2);
        tls = QSslConfiguration::defaultConfiguration(); tls.setLocalCertificateChain(certificates); tls.setPrivateKey(key);
    }
    bool valid; const auto port = parser.value("port").toUShort(&valid);
    const QHostAddress address(parser.value("address")); if (!valid || address.isNull()) parser.showHelp(2);
    iiServerHost::SessionAuthenticator auth(QUrl(parser.value("session-url")));
    iiServerHost::RelayServer server([&](const auto &credential, auto done) { auth.authenticate(credential, std::move(done)); });
    if (!server.listen(address, port, tls)) { QTextStream(stderr) << server.errorString() << Qt::endl; return 1; }
    QTextStream(stdout) << "iiServerHost relay listening on " << address.toString() << ':' << server.port() << Qt::endl;
    return app.exec();
}

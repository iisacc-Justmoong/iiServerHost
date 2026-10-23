#include <iiWebServer.h>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("ii-server-relay"); app.setApplicationVersion("0.3.0");
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{{"a", "address"}, "Listen address; public addresses require TLS.", "address", "127.0.0.1"},
        {{"p", "port"}, "Listen port.", "port", "9443"},
        {"session-url", "Trusted account session verification endpoint.", "url", "https://iisacc.com/Account/GraphQL"},
        {"certificate", "PEM TLS certificate chain.", "path"}, {"key", "PEM TLS private key.", "path"}});
    parser.process(app);
    iiWebServer::TlsConfiguration tls;
    if (parser.isSet("certificate") || parser.isSet("key")) {
        tls.policy.identity.certificate_chain_file = parser.value("certificate").toStdString();
        tls.policy.identity.private_key_file = parser.value("key").toStdString();
    }
    bool valid; const auto port = parser.value("port").toUShort(&valid);
    const auto address = iiWebServer::Address::parse(parser.value("address").toStdString()); if (!valid || !address) parser.showHelp(2);
    iiWebServer::SessionAuthenticator auth(QUrl(parser.value("session-url")));
    iiWebServer::RelayServer server([&](const auto &credential, auto done) { auth.authenticate(credential, std::move(done)); });
    if (!server.listen(*address, port, tls)) { QTextStream(stderr) << server.errorString() << Qt::endl; return 1; }
    QTextStream(stdout) << "iiWebServer relay listening on " << QString::fromStdString(address->string()) << ':' << server.port() << Qt::endl;
    return app.exec();
}

#include "Transfer.h"
#include "JsonConfig.h"
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QTimer>
#include <csignal>

using namespace iiServerHost;
namespace { volatile std::sig_atomic_t interrupted = 0; void interrupt(int) { interrupted = 1; } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("ii-file-transfer");
    QCommandLineParser parser;
    parser.setApplicationDescription("Stream binary files over installed standard transfer protocols.");
    parser.addHelpOption();
    parser.addOption({"capabilities", "Print the linked backend and available protocols as JSON."});
    parser.addOption({"request", "Owner-only JSON transfer request (credentials are never command-line flags).", "file"});
    parser.process(app);
    if (parser.isSet("capabilities")) {
        QJsonArray protocols;
        for (const auto &p : FileTransfer::protocols()) protocols.append(QJsonObject{
            {"scheme", p.scheme}, {"download", p.download}, {"upload", p.upload}, {"list", p.list}, {"encrypted", p.encrypted}});
        cli::print({{"backend", FileTransfer::backendVersion()}, {"protocols", protocols},
                    {"httpVersions", QJsonArray::fromStringList(FileTransfer::httpVersions())}}); return 0;
    }
    QString error;
    const auto object = cli::readPrivateObject(parser.value("request"), error);
    if (!error.isEmpty()) return cli::failure(error);
    TransferRequest request;
    const auto operation = object["operation"].toString();
    if (operation == "download") request.operation = TransferOperation::Download;
    else if (operation == "upload") request.operation = TransferOperation::Upload;
    else if (operation == "list") request.operation = TransferOperation::List;
    else return cli::failure("unsupported_operation");
    request.url = QUrl(object["url"].toString(), QUrl::StrictMode);
    request.localPath = object["localPath"].toString();
    request.username = object["username"].toString();
    request.password = object["password"].toString().toUtf8();
    request.bearerToken = object["bearerToken"].toString().toUtf8();
    request.caFile = object["caFile"].toString();
    request.clientCertificate = object["clientCertificate"].toString();
    request.clientKey = object["clientKey"].toString();
    request.sshKnownHosts = object["sshKnownHosts"].toString();
    request.sshPrivateKey = object["sshPrivateKey"].toString();
    request.sshPublicKey = object["sshPublicKey"].toString();
    request.keyPassword = object["keyPassword"].toString().toUtf8();
    request.awsSigV4 = object["awsSigV4"].toString();
    request.httpVersion = object["httpVersion"].toString();
    request.allowCleartext = object["allowCleartext"].toBool(false);
    if (object.contains("maximumBytes")) {
        bool valid = false;
        request.maximumBytes = object["maximumBytes"].toString().toLongLong(&valid);
        if (!valid) return cli::failure("invalid_maximum_bytes");
    }
    request.expectedSha256 = object["expectedSha256"].toString().toLatin1();
    request.timeoutMs = object["timeoutMs"].toInt(request.timeoutMs);
    request.connectTimeoutMs = object["connectTimeoutMs"].toInt(request.connectTimeoutMs);
    FileTransfer transfer;
    QObject::connect(&transfer, &FileTransfer::finished, &app, [&](const QString &, const TransferResult &r) {
        cli::print({{"ok", r.ok}, {"cancelled", r.cancelled}, {"error", r.error}, {"protocol", r.protocol},
            {"bytes", QString::number(r.bytes)}, {"sha256", QString::fromLatin1(r.sha256)}, {"responseCode", r.responseCode},
            {"httpVersion", r.httpVersion}});
        app.exit(r.ok ? 0 : 1);
    });
    const auto id = transfer.start(request);
    std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] { if (interrupted) transfer.cancel(id); });
    timer.start(100);
    return app.exec();
}

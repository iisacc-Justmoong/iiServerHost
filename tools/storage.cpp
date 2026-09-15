#include "StorageBridge.h"
#include "JsonConfig.h"
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <csignal>

using namespace iiServerHost;
namespace { volatile std::sig_atomic_t interrupted = 0; void interrupt(int) { interrupted = 1; } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("ii-storage");
    QCommandLineParser parser;
    parser.setApplicationDescription("Transfer between NAS/cloud backends or host standard file protocols through rclone.");
    parser.addHelpOption();
    parser.addOption({"config", "Owner-only JSON containing backend and operation/server settings.", "file"});
    parser.addOption({"serve", "Run the configured file server until interrupted."});
    parser.process(app);
    QString error;
    const auto config = cli::readPrivateObject(parser.value("config"), error);
    if (!error.isEmpty()) return cli::failure(error);
    const auto b = config["backend"].toObject();
    StorageBridgeOptions backend{b["executable"].toString(), b["configFile"].toString(),
        b["configPassword"].toString().toUtf8(), b["runtimeDirectory"].toString()};
    backend.ftpPythonExecutable = b["ftpPythonExecutable"].toString();
    backend.ftpServerScript = b["ftpServerScript"].toString();
    std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
    QTimer timer; timer.start(100);
    if (parser.isSet("serve")) {
        const auto o = config["server"].toObject();
        FileServerOptions options;
        const QMap<QString, FileServerProtocol> protocols{{"http", FileServerProtocol::Http}, {"https", FileServerProtocol::Https},
            {"webdav", FileServerProtocol::WebDav}, {"webdavs", FileServerProtocol::WebDavs}, {"ftp", FileServerProtocol::Ftp},
            {"ftps", FileServerProtocol::Ftps}, {"sftp", FileServerProtocol::Sftp}, {"s3", FileServerProtocol::S3},
            {"ftpes", FileServerProtocol::FtpES},
            {"s3s", FileServerProtocol::S3Tls}, {"restic", FileServerProtocol::Restic}, {"restics", FileServerProtocol::ResticTls},
            {"nfs", FileServerProtocol::Nfs}};
        const auto protocol = o["protocol"].toString();
        if (!protocols.contains(protocol)) return cli::failure("unsupported_protocol");
        options.protocol = protocols.value(protocol);
        options.root = o["root"].toString();
        options.address = QHostAddress(o["address"].toString("127.0.0.1"));
        const auto port = o["port"].toInt();
        if (port < 1 || port > 65535) return cli::failure("invalid_port");
        options.port = quint16(port);
        options.username = o["username"].toString(); options.password = o["password"].toString().toUtf8();
        options.certificate = o["certificate"].toString(); options.privateKey = o["privateKey"].toString();
        options.authorizedKeys = o["authorizedKeys"].toString(); options.readOnly = o["readOnly"].toBool(true);
        options.allowCleartext = o["allowCleartext"].toBool(false);
        options.passivePorts = o["passivePorts"].toString(options.passivePorts); options.publicAddress = o["publicAddress"].toString();
        FileProtocolServer server(backend);
        QObject::connect(&server, &FileProtocolServer::started, &app, [] { cli::print({{"launched", true}}); });
        QObject::connect(&server, &FileProtocolServer::failed, &app, [&](const QString &error) { cli::failure(error); app.exit(1); });
        QObject::connect(&server, &FileProtocolServer::stopped, &app, [&] { if (interrupted) app.quit(); });
        QObject::connect(&timer, &QTimer::timeout, &app, [&] { if (interrupted) server.stop(); });
        if (!server.start(options)) return cli::failure(server.errorString());
        return app.exec();
    }
    const auto o = config["request"].toObject();
    const QMap<QString, StorageOperation> operations{{"providers", StorageOperation::Providers}, {"list", StorageOperation::List},
        {"stat", StorageOperation::Stat}, {"copy-file", StorageOperation::CopyFile}, {"copy-directory", StorageOperation::CopyDirectory},
        {"mkdir", StorageOperation::MakeDirectory}, {"remove-file", StorageOperation::RemoveFile}, {"check", StorageOperation::Check}};
    if (!operations.contains(o["operation"].toString())) return cli::failure("unsupported_operation");
    StorageRequest request{operations.value(o["operation"].toString()), o["source"].toString(), o["destination"].toString(),
        o["overwrite"].toBool(false), o["timeoutMs"].toInt(300000)};
    StorageBridge bridge(backend);
    QObject::connect(&bridge, &StorageBridge::finished, &app, [&](const QString &, const StorageResult &r) {
        cli::print({{"ok", r.ok}, {"cancelled", r.cancelled}, {"error", r.error}, {"exitCode", r.exitCode}, {"data", r.data}});
        app.exit(r.ok ? 0 : 1);
    });
    const auto id = bridge.start(request);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] { if (interrupted) bridge.cancel(id); });
    return app.exec();
}

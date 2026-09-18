#pragma once

#include "iiServerHostExport.h"
#include <QtCore/QJsonValue>
#include <QtCore/QObject>
#include <QtNetwork/QHostAddress>
#include <memory>

namespace iiServerHost {

struct StorageBridgeOptions {
    QString executable; // Absolute rclone executable; empty = discover on PATH.
    QString configFile; // Explicit, owner-only configuration. Empty = no configured remotes.
    QByteArray configPassword; // For an encrypted rclone config, child environment only.
    QString runtimeDirectory; // Existing private directory for cache/temp data, outside exported roots.
    QString ftpPythonExecutable; // Optional Python with pyftpdlib + pyOpenSSL for fully protected FTPS/FTPES.
    QString ftpServerScript; // Installed share/iiServerHost/ftp_server.py (explicit trusted path).
};

enum class StorageOperation { Providers, List, Stat, CopyFile, CopyDirectory, MakeDirectory, RemoveFile, Check };
struct StorageRequest {
    StorageOperation operation = StorageOperation::List;
    QString source; // Absolute local path or a named remote:path from the explicit config.
    QString destination;
    bool overwrite = false;
    int timeoutMs = 300000;
};
struct StorageResult {
    bool ok = false;
    bool cancelled = false;
    int exitCode = -1;
    QString error; // Stable code; never the backend's credential-bearing diagnostic text.
    QJsonValue data; // Provider catalogue, directory listing, or stat result.
};

/// Noninteractive access to every backend compiled into the selected rclone.
/// Four concurrent jobs, explicit config, no shell and no inherited RCLONE_* settings.
class IISERVERHOST_EXPORT StorageBridge final : public QObject {
    Q_OBJECT
public:
    explicit StorageBridge(StorageBridgeOptions options, QObject *parent = nullptr);
    ~StorageBridge() override;
    QString start(const StorageRequest &request);
    void cancel(const QString &id);
signals:
    void finished(QString id, iiServerHost::StorageResult result);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

enum class FileServerProtocol { Http, Https, WebDav, WebDavs, Ftp, Ftps, FtpES, Sftp, S3, S3Tls, Restic, ResticTls, Nfs };
struct FileServerOptions {
    FileServerProtocol protocol = FileServerProtocol::WebDavs;
    QString root; // Absolute existing directory, or configured remote:path.
    QHostAddress address = QHostAddress::LocalHost;
    quint16 port = 0; // Required, explicit port.
    QString username; // S3: access key ID. Other authenticated servers: account-scoped service user.
    QByteArray password; // S3: secret key. Child environment, never command-line arguments.
    QString certificate;
    QString privateKey; // TLS key, or persistent SSH host key for SFTP.
    QString authorizedKeys; // Optional SFTP client public keys.
    bool readOnly = true;
    bool allowCleartext = false; // Required for plain protocols on non-loopback interfaces.
    QString passivePorts = "30000-32000"; // FTP data ports; configure firewall/NAT separately.
    QString publicAddress; // Optional FTP passive-mode advertised IP.
};

/// Managed standard-protocol host. Auth is explicit and independent of Society account sessions.
/// Each object owns one child; stop/destruction terminates it. Backend failure is reported, not restarted silently.
class IISERVERHOST_EXPORT FileProtocolServer final : public QObject {
    Q_OBJECT
public:
    explicit FileProtocolServer(StorageBridgeOptions backend, QObject *parent = nullptr);
    ~FileProtocolServer() override;
    bool start(const FileServerOptions &options);
    void stop();
    bool isRunning() const;
    QString errorString() const;
signals:
    void started(); // Child launched; protocol readiness/authentication must be checked by the client.
    void stopped();
    void failed(QString error);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace iiServerHost
Q_DECLARE_METATYPE(iiServerHost::StorageResult)

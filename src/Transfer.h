#pragma once

#include "iiServerHostExport.h"
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <memory>

namespace iiServerHost {

enum class TransferOperation { Download, Upload, List };

struct TransferProtocol {
    QString scheme;
    bool download = false;
    bool upload = false;
    bool list = false;
    bool encrypted = false;
};

struct TransferRequest {
    QUrl url;
    TransferOperation operation = TransferOperation::Download;
    QString localPath; // Download/list output or upload input. Binary, no extension restrictions.
    QString username;
    QByteArray password;
    QByteArray bearerToken;
    QString caFile;
    QString clientCertificate;
    QString clientKey;
    QString sshKnownHosts; // Required for SFTP/SCP; unknown/changed host keys fail.
    QString sshPrivateKey;
    QString sshPublicKey;
    QByteArray keyPassword;
    QString awsSigV4; // e.g. aws:amz:us-east-1:s3, with username/password as access/secret keys.
    QString httpVersion; // Empty = automatic; "1.0", "1.1", "2", "3". HTTP/2 and /3 permit fallback.
    bool allowCleartext = false; // Plain protocols are otherwise limited to literal loopback.
    qint64 maximumBytes = 0; // 0 = unlimited, streamed rather than buffered in memory.
    QByteArray expectedSha256; // Optional 64 hex characters. Failed downloads preserve the old file.
    int connectTimeoutMs = 10000;
    int timeoutMs = 300000;
};

struct TransferResult {
    bool ok = false;
    bool cancelled = false;
    QString error; // Stable error code; contains no URL, credentials, server body or curl diagnostics.
    QString protocol;
    qint64 bytes = 0;
    QByteArray sha256;
    int responseCode = 0;
    QString httpVersion; // Actually negotiated HTTP version, empty for non-HTTP transfers.
};

/// Owner-thread API; each transfer runs independently of the Qt UI event loop.
/// Up to four active transfers; every start returns an ID and completes asynchronously once.
class IISERVERHOST_EXPORT FileTransfer final : public QObject {
    Q_OBJECT
public:
    explicit FileTransfer(QObject *parent = nullptr);
    ~FileTransfer() override;
    static QList<TransferProtocol> protocols(); // Intersection with the linked libcurl's capabilities.
    static QString backendVersion(); // Empty when built without libcurl.
    static QStringList httpVersions();
    QString start(const TransferRequest &request);
    void cancel(const QString &id);
    int activeCount() const;
signals:
    void progress(QString id, qint64 bytes, qint64 total);
    void finished(QString id, iiServerHost::TransferResult result);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace iiServerHost
Q_DECLARE_METATYPE(iiServerHost::TransferResult)

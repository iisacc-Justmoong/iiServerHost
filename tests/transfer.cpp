#include "Transfer.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace iiServerHost;

class TransferTests : public QObject {
    Q_OBJECT
private slots:
    void binaryRoundTripAndIntegrity();
    void failedDownloadPreservesDestination();
    void policyRejectsBeforeIO();
    void httpErrorsRedirectsAndCancellation();
};

static bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static TransferResult waitResult(FileTransfer &transfer, const TransferRequest &request) {
    QSignalSpy spy(&transfer, &FileTransfer::finished);
    const auto id = transfer.start(request);
    if (spy.isEmpty()) spy.wait(10000);
    if (spy.size() != 1 || spy[0][0].toString() != id) return {false, false, "test_timeout"};
    return qvariant_cast<TransferResult>(spy[0][1]);
}

void TransferTests::binaryRoundTripAndIntegrity() {
    if (FileTransfer::backendVersion().isEmpty()) QSKIP("libcurl disabled");
    QTemporaryDir dir(QString(TEST_DIRECTORY) + "/transfer-XXXXXX");
    QVERIFY(dir.isValid());
    QByteArray bytes(3 * 1024 * 1024 + 123, '\0');
    for (qsizetype i = 0; i < bytes.size(); ++i) bytes[i] = char(i % 256);
    QVERIFY(writeFile(dir.filePath("소스 model.bin"), bytes));
    FileTransfer transfer;
    TransferRequest request;
    request.url = QUrl::fromLocalFile(dir.filePath("소스 model.bin"));
    request.localPath = dir.filePath("out.any-extension");
    request.expectedSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    auto result = waitResult(transfer, request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.bytes, bytes.size());
    QCOMPARE(result.sha256, request.expectedSha256);
    QCOMPARE(readFile(request.localPath), bytes);
    request.operation = TransferOperation::Upload;
    request.url = QUrl::fromLocalFile(dir.filePath("uploaded"));
    result = waitResult(transfer, request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(readFile(request.url.toLocalFile()), bytes);
    QCOMPARE(transfer.activeCount(), 0);
    // Zero-byte files are still files and must not be treated as failed reads.
    QVERIFY(writeFile(dir.filePath("empty"), {}));
    request.operation = TransferOperation::Download;
    request.url = QUrl::fromLocalFile(dir.filePath("empty"));
    request.expectedSha256 = QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256).toHex();
    result = waitResult(transfer, request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.bytes, 0);
    QCOMPARE(readFile(request.localPath), QByteArray());
}

void TransferTests::failedDownloadPreservesDestination() {
    if (FileTransfer::backendVersion().isEmpty()) QSKIP("libcurl disabled");
    QTemporaryDir dir(QString(TEST_DIRECTORY) + "/transfer-XXXXXX");
    QVERIFY(writeFile(dir.filePath("source"), QByteArray(400000, 'x')));
    const auto destination = dir.filePath("destination");
    QVERIFY(writeFile(destination, "keep"));
    FileTransfer transfer;
    TransferRequest request;
    request.url = QUrl::fromLocalFile(dir.filePath("source"));
    request.localPath = destination;
    request.expectedSha256 = QByteArray(64, '0');
    QCOMPARE(waitResult(transfer, request).error, "checksum_mismatch");
    QCOMPARE(readFile(destination), "keep");
    request.expectedSha256.clear();
    request.maximumBytes = 1024;
    QVERIFY(!waitResult(transfer, request).ok);
    QCOMPARE(readFile(destination), "keep");
}

void TransferTests::policyRejectsBeforeIO() {
    FileTransfer transfer;
    if (FileTransfer::backendVersion().isEmpty()) {
        QCOMPARE(waitResult(transfer, {}).error, "backend_unavailable");
        QVERIFY(FileTransfer::protocols().isEmpty());
        return;
    }
    TransferRequest r;
    r.url = QUrl("http://example.com/file");
    r.localPath = "/nonexistent/destination";
    QCOMPARE(waitResult(transfer, r).error, "cleartext_disabled");
    r.url = QUrl("https://user:secret@example.com/file");
    QCOMPARE(waitResult(transfer, r).error, "invalid_url");
    r.url = QUrl("telnet://example.com/");
    QCOMPARE(waitResult(transfer, r).error, "unsupported_protocol");
    r.url = QUrl("https://example.com/file");
    r.httpVersion = "invalid";
    QCOMPARE(waitResult(transfer, r).error, "unsupported_http_version");
    r.httpVersion.clear();
    r.expectedSha256 = "invalid";
    QCOMPARE(waitResult(transfer, r).error, "invalid_checksum");
    for (const auto &p : FileTransfer::protocols()) {
        QVERIFY(p.download || p.upload);
        if (p.scheme == "sftp") {
            r.expectedSha256.clear();
            r.url = QUrl("sftp://example.com/file");
            QCOMPARE(waitResult(transfer, r).error, "ssh_known_hosts_required");
        }
    }
}

void TransferTests::httpErrorsRedirectsAndCancellation() {
    if (FileTransfer::backendVersion().isEmpty()) QSKIP("libcurl disabled");
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int requests = 0;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests] {
            const auto data = socket->readAll();
            if (!data.contains("\r\n\r\n")) return;
            ++requests;
            if (data.startsWith("GET /slow")) return;
            if (data.startsWith("GET /redirect"))
                socket->write("HTTP/1.1 302 Found\r\nLocation: file:///etc/passwd\r\nContent-Length: 0\r\n\r\n");
            else socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 6\r\n\r\nsecret");
            socket->disconnectFromHost();
        });
    });
    QTemporaryDir dir(QString(TEST_DIRECTORY) + "/transfer-XXXXXX");
    FileTransfer transfer;
    TransferRequest r;
    r.url = QUrl(QString("http://127.0.0.1:%1/missing").arg(server.serverPort()));
    r.localPath = dir.filePath("old");
    QVERIFY(writeFile(r.localPath, "old"));
    const auto missing = waitResult(transfer, r);
    QCOMPARE(missing.responseCode, 404);
    QVERIFY(!missing.ok);
    QCOMPARE(readFile(r.localPath), "old");
    r.url.setPath("/redirect");
    QCOMPARE(waitResult(transfer, r).error, "redirect_requires_new_request");
    QCOMPARE(readFile(r.localPath), "old");
    QCOMPARE(requests, 2);
    r.url.setPath("/slow");
    QSignalSpy spy(&transfer, &FileTransfer::finished);
    auto id = transfer.start(r);
    QTRY_COMPARE_WITH_TIMEOUT(requests, 3, 5000);
    transfer.cancel(id);
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000);
    QVERIFY(qvariant_cast<TransferResult>(spy[0][1]).cancelled);
    QCOMPARE(readFile(r.localPath), "old");
    QCOMPARE(transfer.activeCount(), 0);
}

QTEST_GUILESS_MAIN(TransferTests)
#include "transfer.moc"

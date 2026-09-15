#include "StorageBridge.h"
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace iiServerHost;
class StorageBridgeTests : public QObject {
    Q_OBJECT
private slots:
    void actualCatalogueAndFileOperations();
    void rejectUnsafeConfiguration();
    void serverRequiresAuthentication();
};
static StorageResult run(StorageBridge &bridge, const StorageRequest &request) {
    QSignalSpy spy(&bridge, &StorageBridge::finished);
    const auto id = bridge.start(request);
    if (spy.isEmpty()) spy.wait(30000);
    if (spy.size() != 1 || spy[0][0].toString() != id) return {false, false, -1, "test_timeout"};
    return qvariant_cast<StorageResult>(spy[0][1]);
}
void StorageBridgeTests::actualCatalogueAndFileOperations() {
    const auto executable = qEnvironmentVariable("IISERVERHOST_TEST_RCLONE");
    if (executable.isEmpty()) QSKIP("Set IISERVERHOST_TEST_RCLONE for real backend integration tests");
    QTemporaryDir dir(QString(TEST_DIRECTORY) + "/storage-XXXXXX");
    QVERIFY(dir.isValid());
    StorageBridge bridge({executable, {}, {}, dir.path()});
    auto providers = run(bridge, {StorageOperation::Providers});
    QVERIFY2(providers.ok, qPrintable(providers.error));
    QVERIFY(providers.data.isArray());
    QStringList names;
    for (const auto &value : providers.data.toArray()) names.append(value.toObject()["Name"].toString());
    for (const auto &name : {"sftp", "smb", "s3", "webdav", "ftp", "azureblob", "drive"}) QVERIFY(names.contains(name));
    QFile source(dir.filePath("model.bin"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray bytes(1500000, char(0xff));
    QCOMPARE(source.write(bytes), bytes.size()); source.close();
    const auto destination = dir.filePath("out.bin");
    auto copied = run(bridge, {StorageOperation::CopyFile, source.fileName(), destination});
    QVERIFY2(copied.ok, qPrintable(copied.error));
    QFile file(destination); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
    auto stat = run(bridge, {StorageOperation::Stat, destination});
    QVERIFY2(stat.ok, qPrintable(stat.error));
    QCOMPARE(stat.data.toObject()["Size"].toInteger(), bytes.size());
    auto list = run(bridge, {StorageOperation::List, dir.path()});
    QVERIFY(list.ok); QVERIFY(list.data.isArray());
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate)); source.write("changed"); source.close();
    QVERIFY(run(bridge, {StorageOperation::CopyFile, source.fileName(), destination}).ok);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
    auto removed = run(bridge, {StorageOperation::RemoveFile, destination});
    QVERIFY(removed.ok); QVERIFY(!file.exists());
}
void StorageBridgeTests::rejectUnsafeConfiguration() {
    QTemporaryDir dir(QString(TEST_DIRECTORY) + "/storage-XXXXXX");
    StorageBridge missing({dir.filePath("missing-rclone"), {}, {}, dir.path()});
    QCOMPARE(run(missing, {StorageOperation::Providers}).error, "backend_unavailable");
    const auto executable = qEnvironmentVariable("IISERVERHOST_TEST_RCLONE");
    if (executable.isEmpty()) return;
    StorageBridge bridge({executable, {}, {}, dir.path()});
    QCOMPARE(run(bridge, {StorageOperation::List, "--config=/private/file"}).error, "invalid_location");
    QCOMPARE(run(bridge, {StorageOperation::List, ":sftp,host=example.com:/"}).error, "invalid_location");
    QCOMPARE(run(bridge, {StorageOperation::List, "remote:path"}).error, "explicit_config_required");
    const auto config = dir.filePath("open.conf");
    QFile f(config); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("[test]\ntype = local\n"); f.close();
    QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
    StorageBridge unsafe({executable, config, {}, dir.path()});
    QCOMPARE(run(unsafe, {StorageOperation::Providers}).error, "private_config_required");
}
void StorageBridgeTests::serverRequiresAuthentication() {
    const auto executable = qEnvironmentVariable("IISERVERHOST_TEST_RCLONE");
    if (executable.isEmpty()) QSKIP("Set IISERVERHOST_TEST_RCLONE");
    QTemporaryDir dir(QString(TEST_DIRECTORY) + "/storage-XXXXXX");
    QVERIFY(QDir(dir.path()).mkdir("files"));
    FileProtocolServer server({executable, {}, {}, dir.path()});
    FileServerOptions options;
    options.root = dir.filePath("files"); options.port = 19876; options.protocol = FileServerProtocol::WebDav;
    QVERIFY(!server.start(options)); QCOMPARE(server.errorString(), "authentication_required");
    options.username = "account"; options.password = "password"; options.address = QHostAddress::AnyIPv4;
    QVERIFY(!server.start(options)); QCOMPARE(server.errorString(), "cleartext_disabled");
    options.protocol = FileServerProtocol::WebDavs;
    QVERIFY(!server.start(options)); QCOMPARE(server.errorString(), "tls_identity_required");
    options.protocol = FileServerProtocol::Sftp;
    QVERIFY(!server.start(options)); QCOMPARE(server.errorString(), "ssh_host_key_required");
    QVERIFY(!server.isRunning());
}
QTEST_GUILESS_MAIN(StorageBridgeTests)
#include "storage_bridge.moc"

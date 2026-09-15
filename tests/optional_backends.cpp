#include "iiServerHost.h"
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

class OptionalBackendTests : public QObject {
    Q_OBJECT
private slots:
    void featureConfigurationMatchesLinkedArtifact() {
        using namespace iiServerHost;
        QCOMPARE(!FileTransfer::backendVersion().isEmpty(), bool(TEST_CURL_ENABLED));
        QCOMPARE(!FileTransfer::protocols().isEmpty(), bool(TEST_CURL_ENABLED));
        if (!TEST_CURL_ENABLED) {
            FileTransfer transfer;
            QSignalSpy finished(&transfer, &FileTransfer::finished);
            transfer.start({});
            QTRY_COMPARE(finished.size(), 1);
            QCOMPARE(qvariant_cast<TransferResult>(finished[0][1]).error, "backend_unavailable");
        }
        StorageBridgeOptions backend;
        backend.executable = "/nonexistent/iiServerHost-fixture-rclone";
        StorageBridge storage(backend);
        QSignalSpy finished(&storage, &StorageBridge::finished);
        storage.start({StorageOperation::Providers});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(qvariant_cast<StorageResult>(finished[0][1]).error,
                 TEST_PROCESS_ENABLED ? "backend_unavailable" : "external_backend_disabled");
        FileProtocolServer server(backend);
        QVERIFY(!server.start({}));
        QCOMPARE(server.errorString(), TEST_PROCESS_ENABLED ? "backend_unavailable" : "external_backend_disabled");
    }
};
QTEST_GUILESS_MAIN(OptionalBackendTests)
#include "optional_backends.moc"

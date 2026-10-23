#pragma once
#include "NetworkTypes.h"
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <memory>

namespace iiWebServer {
// GUI-facing signal adapter. All wire, socket and TLS work belongs to iiNetwork.
class IIWEBSERVER_EXPORT WebSocket final : public QObject {
    Q_OBJECT
public:
    explicit WebSocket(QObject *parent = nullptr);
    ~WebSocket() override;
    void open(const QUrl &);
    void abort();
    void close(quint16 code = 1000, const QString &reason = {});
    qint64 sendTextMessage(const QString &);
    qint64 sendBinaryMessage(const QByteArray &);
    void ping();
    ii::network::SocketState state() const;
    qint64 bytesToWrite() const;
    QString errorString() const;
    QString peerAddress() const;
    QString peerCertificateFingerprint() const;
    void setTlsConfiguration(TlsConfiguration);
signals:
    void connected();
    void disconnected();
    void errorOccurred();
    void textMessageReceived(QString);
    void binaryMessageReceived(QByteArray);
private:
    friend class WebSocketServer;
    struct Private;
    std::shared_ptr<Private> d;
};
class IIWEBSERVER_EXPORT WebSocketServer final : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;
    bool listen(const Address &, quint16, const TlsConfiguration &);
    quint16 serverPort() const;
    bool hasPendingConnections() const;
    WebSocket *nextPendingConnection();
    QString errorString() const;
signals:
    void newConnection();
private:
    struct Private;
    std::unique_ptr<Private> d;
};
}

#pragma once
#include "ServerHost.h"
#include <QStringList>

namespace iiServerHost {
// A local-only, untrusted QR payload. It contains no account credential.
struct IISERVERHOST_EXPORT LanLink {
    QStringList addresses;
    quint16 port = 0;
    QString hostId, name, fingerprint, code;
    QDateTime expiresAt;
    QString encode() const;
    static bool decode(const QString &text, LanLink *result);
    static bool localAddress(const QString &address);
};

class IISERVERHOST_EXPORT LanPeer final : public QObject {
    Q_OBJECT
public:
    explicit LanPeer(QObject *parent = nullptr);
    ~LanPeer() override;
    // Desktop only. Empty addresses enumerate active Wi-Fi/Ethernet interfaces.
    bool startHost(QString hostId, QString name, RequestHandler handler,
                   QStringList addresses = {}, QHostAddress bindAddress = QHostAddress::AnyIPv4);
    QString createOffer(int lifetimeSeconds = 60);
    // Discovery offers bind a candidate and require a human comparison of the
    // TLS-bound code before the host exposes any Files metadata or bytes.
    QString createDeviceOffer(const QString &peerId, int lifetimeSeconds = 60);
    QString verificationCode() const;
    bool confirmDevice();
    bool join(const QString &qr, QString peerId, QString name);
    void cancelPairing();
    void stop();
    bool hosting() const;
    bool connected() const;
    QString phase() const;
    QString errorString() const;
    QString qrText() const;
    QString peerName() const;
    int secondsRemaining() const;
    QJsonArray peers() const;
    QStringList pairedDeviceIds() const;
    QString request(const QString &host, const QJsonObject &payload);
signals:
    void changed();
    void paired(QString peerId);
    void completed(QString id, QJsonObject result, QString transport);
private:
    class Private;
    std::unique_ptr<Private> d;
};
}

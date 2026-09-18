#include "ServerHost.h"
#include "Protocol.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

namespace iiServerHost {
class SessionAuthenticator::Private {
public:
    QUrl url;
    QNetworkAccessManager network;
};
SessionAuthenticator::SessionAuthenticator(QUrl url, QObject *parent) : QObject(parent), d(std::make_unique<Private>()) { d->url = std::move(url); }
SessionAuthenticator::~SessionAuthenticator() = default;
void SessionAuthenticator::authenticate(const QByteArray &cookie, AuthCompletion done) {
    if (!protocol::endpoint(d->url, "https", "http") || cookie.isEmpty() || cookie.size() > 16384
        || cookie.contains('\r') || cookie.contains('\n') || cookie.contains('\0')) { done({}); return; }
    QNetworkRequest request(d->url);
    request.setRawHeader("Cookie", cookie);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(5000);
    auto *reply = d->network.get(request);
    reply->setReadBufferSize(65537);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, reply, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > 65536) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, bytes, done = std::move(done)] {
        bytes->append(reply->readAll());
        Principal principal;
        if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && bytes->size() <= 65536) {
            const auto account = QJsonDocument::fromJson(*bytes).object().value("account").toObject();
            principal.accountId = account.value("sub").toString();
            principal.expiresAt = QDateTime::currentDateTimeUtc().addSecs(60);
            if (!protocol::live(principal)) principal = {};
        }
        reply->deleteLater();
        done(principal);
    });
}
}

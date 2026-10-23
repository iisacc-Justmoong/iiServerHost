#include "WebServer.h"
#include "Protocol.h"
#include <iiNetwork/Http.h>
#include <QPointer>
#include <QTimer>

namespace iiWebServer {
class SessionAuthenticator::Private {
public:
    QUrl url;
    ii::runtime::EventLoop loop{4096};
    QTimer timer;
    QHash<QString, std::shared_ptr<ii::network::Http>> requests;
};
SessionAuthenticator::SessionAuthenticator(QUrl url, QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {
    d->url = std::move(url); d->timer.setInterval(2); d->timer.setTimerType(Qt::PreciseTimer);
    connect(&d->timer, &QTimer::timeout, this, [this] { d->loop.poll(); });
}
SessionAuthenticator::~SessionAuthenticator() = default;
void SessionAuthenticator::authenticate(const QByteArray &cookie, AuthCompletion done) {
    if (!protocol::endpoint(d->url, "https", "http") || cookie.isEmpty() || cookie.size() > 16384
        || cookie.contains('\r') || cookie.contains('\n') || cookie.contains('\0') || d->requests.size() >= 64) { done({}); return; }
    ii::network::HttpRequest request;
    request.url = d->url.toEncoded().toStdString(); request.method = "POST";
    request.headers = {{"Cookie", cookie.toStdString()}, {"Accept", "application/json"}, {"Content-Type", "application/json"},
        {"Origin", d->url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toEncoded().toStdString()}};
    request.follow_redirects = false; request.enable_cookies = false;
    request.proxy.type = ii::network::ProxyType::disabled;
    request.timeout = std::chrono::milliseconds(5000); request.maximum_bytes = 65536;
    request.body = R"({"query":"query AccountSession { accountSession { account { sub } } }"})";
    const auto id = protocol::uuid();
    auto http = std::make_shared<ii::network::Http>(d->loop); d->requests.insert(id, http);
    const QPointer<SessionAuthenticator> owner(this);
    auto result = http->request(std::move(request), [owner, id, done](ii::network::HttpResponse response) {
        if (!owner) return;
        Principal principal;
        if (response.error.empty() && response.status == 200 && response.body.size() <= 65536
            && QString::fromStdString(response.content_type).section(';', 0, 0).trimmed().compare("application/json", Qt::CaseInsensitive) == 0) {
            const auto envelope = QJsonDocument::fromJson(QByteArray(response.body.data(), response.body.size())).object();
            const auto account = envelope.contains("errors") ? QJsonObject{}
                : envelope.value("data").toObject().value("accountSession").toObject().value("account").toObject();
            principal.accountId = account.value("sub").toString();
            principal.expiresAt = QDateTime::currentDateTimeUtc().addSecs(60);
            if (!protocol::live(principal)) principal = {};
        }
        owner->d->requests.remove(id);
        if (owner->d->requests.isEmpty()) owner->d->timer.stop();
        done(principal);
    });
    if (!result) { d->requests.remove(id); done({}); return; }
    d->timer.start();
}
}

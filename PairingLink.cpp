#include "PairingLink.h"
#include "Protocol.h"
#include <QUrlQuery>
#include <QSet>

namespace iiServerHost {
namespace {
bool valid(const PairingLink &link) {
    static const QRegularExpression codePattern("\\A[a-f0-9]{64}\\z");
    return protocol::endpoint(link.relayUrl, "wss", "ws") && protocol::identifier(link.hostId)
        && codePattern.match(link.code).hasMatch();
}
}
QString PairingLink::encode() const {
    if (!valid(*this)) return {};
    QUrl url("society://pair"); QUrlQuery query;
    query.addQueryItem("v", "1");
    // Preserve escapes inside the nested URL through the query's decoding pass.
    query.addQueryItem("relay", QString::fromLatin1(QUrl::toPercentEncoding(relayUrl.toString(QUrl::FullyEncoded))));
    query.addQueryItem("host", hostId); query.addQueryItem("code", code);
    url.setQuery(query); const auto text = url.toString(QUrl::FullyEncoded);
    return text.size() <= 2048 ? text : QString();
}
bool PairingLink::decode(const QString &text, PairingLink *result) {
    if (!result || text.size() > 2048) return false;
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "society" || url.host() != "pair" || !url.path().isEmpty()
        || !url.userInfo().isEmpty() || url.port() != -1 || url.hasFragment()) return false;
    const QUrlQuery query(url); const auto items = query.queryItems(QUrl::FullyDecoded);
    QSet<QString> keys;
    for (const auto &item : items) keys.insert(item.first);
    if (items.size() != 4 || keys != QSet<QString>{"v", "relay", "host", "code"}
        || query.queryItemValue("v") != "1") return false;
    PairingLink candidate{QUrl(query.queryItemValue("relay", QUrl::FullyDecoded), QUrl::StrictMode),
        query.queryItemValue("host", QUrl::FullyDecoded), query.queryItemValue("code", QUrl::FullyDecoded)};
    if (!valid(candidate)) return false;
    *result = candidate; return true;
}
}

#pragma once
#include <QSslConfiguration>
namespace iiServerHost {
QSslConfiguration createLanIdentity(const QStringList &addresses);
}

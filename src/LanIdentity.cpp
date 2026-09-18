#include "LanIdentity.h"
#include <QSslKey>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <memory>

namespace iiServerHost {
QSslConfiguration createLanIdentity(const QStringList &addresses) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", size_t(2048)), EVP_PKEY_free);
    std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
    if (!key || !cert || !X509_set_version(cert.get(), 2)
        || !ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1)
        || !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60)
        || !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 86400)
        || !X509_set_pubkey(cert.get(), key.get())) return {};
    auto *subject = X509_get_subject_name(cert.get());
    if (!X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>("Society local device"), -1, -1, 0)
        || !X509_set_issuer_name(cert.get(), subject)) return {};
    QStringList names; for (const auto &address : addresses) names.append("IP:" + address);
    const auto add = [&](int nid, const QByteArray &value) {
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
            X509V3_EXT_conf_nid(nullptr, nullptr, nid, value.constData()), X509_EXTENSION_free);
        return extension && X509_add_ext(cert.get(), extension.get(), -1);
    };
    if (!add(NID_basic_constraints, "critical,CA:FALSE") || !add(NID_key_usage, "critical,digitalSignature,keyEncipherment")
        || !add(NID_ext_key_usage, "serverAuth") || !add(NID_subject_alt_name, names.join(',').toLatin1())
        || !X509_sign(cert.get(), key.get(), EVP_sha256())) return {};
    std::unique_ptr<BIO, decltype(&BIO_free)> certPem(BIO_new(BIO_s_mem()), BIO_free), keyPem(BIO_new(BIO_s_mem()), BIO_free);
    if (!certPem || !keyPem || !PEM_write_bio_X509(certPem.get(), cert.get())
        || !PEM_write_bio_PrivateKey(keyPem.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr)) return {};
    const auto bytes = [](BIO *bio) { char *data = nullptr; const auto size = BIO_get_mem_data(bio, &data); return QByteArray(data, size); };
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificate(QSslCertificate(bytes(certPem.get())));
    auto pem = bytes(keyPem.get()); tls.setPrivateKey(QSslKey(pem, QSsl::Rsa)); pem.fill('\0');
    return tls;
}
}

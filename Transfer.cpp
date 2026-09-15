#include "Transfer.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtNetwork/QHostAddress>
#include <atomic>
#include <algorithm>
#include <functional>
#include <limits>
#ifdef IISERVERHOST_CURL
#include <curl/curl.h>
#endif

namespace iiServerHost {
namespace {
struct Job {
    std::atomic_bool cancelled{false};
    QThread *thread = nullptr;
    TransferResult result;
};

#ifdef IISERVERHOST_CURL
bool curlReady() {
    // One initialization per library lifetime, before any transfer threads start.
    static const auto status = curl_global_init(CURL_GLOBAL_DEFAULT);
    return status == CURLE_OK;
}
QString wireScheme(QString scheme) {
    if (scheme == "webdav") return "http";
    if (scheme == "webdavs") return "https";
    if (scheme == "ftpes") return "ftp";
    return scheme;
}
bool upload(const TransferRequest &r) { return r.operation == TransferOperation::Upload; }

QString validate(const TransferRequest &r) {
    if (r.operation != TransferOperation::Download && r.operation != TransferOperation::Upload && r.operation != TransferOperation::List)
        return "unsupported_operation";
    if (!r.url.isValid() || r.url.isRelative() || !r.url.userInfo().isEmpty() || r.url.hasFragment()
        || r.url.port() == 0 || r.url.toEncoded().size() > 16384)
        return "invalid_url";
    const auto scheme = r.url.scheme();
    if (!r.httpVersion.isEmpty() && (!FileTransfer::httpVersions().contains(r.httpVersion)
        || (wireScheme(scheme) != "http" && wireScheme(scheme) != "https"))) return "unsupported_http_version";
    if (r.httpVersion == "3" && wireScheme(scheme) != "https") return "unsupported_http_version";
    const auto protocols = FileTransfer::protocols();
    const auto p = std::find_if(protocols.begin(), protocols.end(), [&](const auto &p) { return p.scheme == scheme; });
    if (p == protocols.end()) return "unsupported_protocol";
    if ((upload(r) && !p->upload) || (r.operation == TransferOperation::List && !p->list)) return "unsupported_operation";
    if (scheme == "file") {
        if (!r.url.host().isEmpty() || r.url.hasQuery() || !QFileInfo(r.url.toLocalFile()).isAbsolute()) return "invalid_url";
    } else {
        if (r.url.host().isEmpty()) return "invalid_url";
        if (!p->encrypted && !r.allowCleartext && !QHostAddress(r.url.host()).isLoopback()) return "cleartext_disabled";
    }
    if (!r.expectedSha256.isEmpty() && !QRegularExpression("^[a-fA-F0-9]{64}$").match(QString::fromLatin1(r.expectedSha256)).hasMatch())
        return "invalid_checksum";
    if (r.timeoutMs <= 0 || r.connectTimeoutMs <= 0 || r.maximumBytes < 0) return "invalid_limits";
    if ((scheme == "sftp" || scheme == "scp") && (!QFileInfo(r.sshKnownHosts).isAbsolute()
        || !QFileInfo(r.sshKnownHosts).isFile() || !QFileInfo(r.sshKnownHosts).isReadable())) return "ssh_known_hosts_required";
    if (!QFileInfo(r.localPath).isAbsolute() || r.localPath.contains(QChar::Null)) return "invalid_local_path";
    if (r.username.contains(QChar::Null) || r.password.contains('\0') || r.keyPassword.contains('\0')
        || r.bearerToken.contains('\0') || r.bearerToken.contains('\r') || r.bearerToken.contains('\n')) return "invalid_credentials";
    if ((!r.bearerToken.isEmpty() || !r.awsSigV4.isEmpty()) && wireScheme(scheme) != "https" && wireScheme(scheme) != "http")
        return "unsupported_authentication";
    if (!r.awsSigV4.isEmpty() && (!r.bearerToken.isEmpty()
        || !QRegularExpression("^[A-Za-z0-9_-]+:[A-Za-z0-9_-]+:[A-Za-z0-9_-]+:[A-Za-z0-9_-]+$").match(r.awsSigV4).hasMatch()))
        return "invalid_authentication";
    return {};
}

struct IO {
    QFile *input = nullptr;
    QSaveFile *output = nullptr;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    Job *job = nullptr;
    qint64 bytes = 0;
    qint64 maximum = 0;
    QString error;
    bool uploading = false;
    QElapsedTimer lastProgress;
    std::function<void(qint64, qint64)> progress;
};
size_t writeData(char *data, size_t size, size_t count, void *opaque) {
    auto &io = *static_cast<IO *>(opaque);
    if (size && count > size_t(std::numeric_limits<qint64>::max()) / size) return 0;
    const auto n = qint64(size * count);
    if (io.uploading) return size * count; // Do not retain a server's upload response body.
    if (io.job->cancelled) return 0;
    if (n > std::numeric_limits<qint64>::max() - io.bytes || (io.maximum && n > io.maximum - io.bytes)) {
        io.error = "size_limit_exceeded";
        return 0;
    }
    if (io.output->write(data, n) != n) { io.error = "local_write_failed"; return 0; }
    io.hash.addData(QByteArrayView(data, n));
    io.bytes += n;
    return size * count;
}
size_t readData(char *data, size_t size, size_t count, void *opaque) {
    auto &io = *static_cast<IO *>(opaque);
    if (io.job->cancelled || (size && count > size_t(std::numeric_limits<qint64>::max()) / size)) return CURL_READFUNC_ABORT;
    const auto n = io.input->read(data, qint64(size * count));
    if (n < 0) { io.error = "local_read_failed"; return CURL_READFUNC_ABORT; }
    io.hash.addData(QByteArrayView(data, n));
    io.bytes += n;
    return size_t(n);
}
int seekData(void *opaque, curl_off_t offset, int origin) {
    auto &io = *static_cast<IO *>(opaque);
    if (offset != 0 || origin != SEEK_SET || !io.input->seek(0)) return CURL_SEEKFUNC_CANTSEEK;
    io.hash.reset(); io.bytes = 0;
    return CURL_SEEKFUNC_OK;
}
int progressData(void *opaque, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t ulTotal, curl_off_t ulNow) {
    auto &io = *static_cast<IO *>(opaque);
    if (io.job->cancelled) return 1;
    const auto total = io.uploading ? ulTotal : dlTotal;
    if (io.maximum && total > io.maximum) { io.error = "size_limit_exceeded"; return 1; }
    if (io.lastProgress.elapsed() >= 100) {
        io.progress(io.uploading ? ulNow : dlNow, total);
        io.lastProgress.restart();
    }
    return 0;
}

TransferResult perform(const TransferRequest &r, Job &job, std::function<void(qint64, qint64)> progress) {
    TransferResult result;
    result.protocol = r.url.scheme();
    QFile input(r.localPath);
    QSaveFile output(r.localPath);
    output.setDirectWriteFallback(false);
    IO io;
    io.input = &input; io.output = &output; io.job = &job;
    io.uploading = upload(r); io.maximum = r.maximumBytes; io.progress = std::move(progress); io.lastProgress.start();
    if (io.uploading) {
        if (!QFileInfo(r.localPath).isFile() || !input.open(QIODevice::ReadOnly)) { result.error = "local_read_failed"; return result; }
        if (r.maximumBytes && input.size() > r.maximumBytes) { result.error = "size_limit_exceeded"; return result; }
        if (!r.expectedSha256.isEmpty()) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!input.atEnd() && !job.cancelled) {
                const auto bytes = input.read(512 * 1024);
                if (bytes.isEmpty() && input.error() != QFile::NoError) { result.error = "local_read_failed"; return result; }
                hash.addData(bytes);
            }
            if (job.cancelled) { result.cancelled = true; result.error = "cancelled"; return result; }
            if (hash.result().toHex() != r.expectedSha256.toLower()) { result.error = "checksum_mismatch"; return result; }
            if (!input.seek(0)) { result.error = "local_read_failed"; return result; }
        }
    } else if (!output.open(QIODevice::WriteOnly)) { result.error = "local_write_failed"; return result; }

    auto *easy = curl_easy_init();
    auto *multi = curl_multi_init();
    if (!easy || !multi) {
        if (easy) curl_easy_cleanup(easy);
        if (multi) curl_multi_cleanup(multi);
        result.error = "backend_initialization_failed"; return result;
    }
    CURLcode setup = CURLE_OK;
    auto option = [&](CURLoption name, auto value) {
        const auto code = curl_easy_setopt(easy, name, value);
        if (setup == CURLE_OK) setup = code;
    };
    QUrl url = r.url;
    url.setScheme(wireScheme(url.scheme()));
    option(CURLOPT_URL, url.toEncoded().constData());
    option(CURLOPT_PROTOCOLS_STR, url.scheme().toLatin1().constData());
    option(CURLOPT_FOLLOWLOCATION, 0L); // Each new endpoint needs a separate caller decision.
    option(CURLOPT_NETRC, long(CURL_NETRC_IGNORED));
    option(CURLOPT_PROXY, ""); // Do not inherit ambient credentials/proxy configuration.
    option(CURLOPT_NOSIGNAL, 1L);
    option(CURLOPT_SSL_VERIFYPEER, 1L);
    option(CURLOPT_SSL_VERIFYHOST, 2L);
    option(CURLOPT_SSLVERSION, long(CURL_SSLVERSION_TLSv1_2));
    option(CURLOPT_CONNECTTIMEOUT_MS, long(r.connectTimeoutMs));
    option(CURLOPT_TIMEOUT_MS, long(r.timeoutMs));
    option(CURLOPT_FAILONERROR, 1L);
    if (r.httpVersion == "1.0") option(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_0));
    else if (r.httpVersion == "1.1") option(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
    else if (r.httpVersion == "2") option(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_2_0));
    else if (r.httpVersion == "3") option(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_3));
    option(CURLOPT_WRITEFUNCTION, &writeData);
    option(CURLOPT_WRITEDATA, &io);
    option(CURLOPT_XFERINFOFUNCTION, &progressData);
    option(CURLOPT_XFERINFODATA, &io);
    option(CURLOPT_NOPROGRESS, 0L);
    if (r.maximumBytes) option(CURLOPT_MAXFILESIZE_LARGE, curl_off_t(r.maximumBytes));
    if (!r.username.isEmpty()) option(CURLOPT_USERNAME, r.username.toUtf8().constData());
    if (!r.password.isEmpty()) option(CURLOPT_PASSWORD, r.password.constData());
    if (!r.caFile.isEmpty()) option(CURLOPT_CAINFO, QFile::encodeName(r.caFile).constData());
    if (!r.clientCertificate.isEmpty()) option(CURLOPT_SSLCERT, QFile::encodeName(r.clientCertificate).constData());
    if (!r.clientKey.isEmpty()) option(CURLOPT_SSLKEY, QFile::encodeName(r.clientKey).constData());
    if (!r.keyPassword.isEmpty()) option(CURLOPT_KEYPASSWD, r.keyPassword.constData());
    if (!r.sshKnownHosts.isEmpty()) option(CURLOPT_SSH_KNOWNHOSTS, QFile::encodeName(r.sshKnownHosts).constData());
    if (!r.sshPrivateKey.isEmpty()) option(CURLOPT_SSH_PRIVATE_KEYFILE, QFile::encodeName(r.sshPrivateKey).constData());
    if (!r.sshPublicKey.isEmpty()) option(CURLOPT_SSH_PUBLIC_KEYFILE, QFile::encodeName(r.sshPublicKey).constData());
    if (!r.awsSigV4.isEmpty()) option(CURLOPT_AWS_SIGV4, r.awsSigV4.toLatin1().constData());
    if (r.url.scheme() == "ftpes" || r.url.scheme() == "ftps") option(CURLOPT_USE_SSL, long(CURLUSESSL_ALL));
    curl_slist *headers = nullptr;
    if (!r.bearerToken.isEmpty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + r.bearerToken).constData());
    if (io.uploading) {
        option(CURLOPT_UPLOAD, 1L);
        option(CURLOPT_READFUNCTION, &readData);
        option(CURLOPT_READDATA, &io);
        option(CURLOPT_SEEKFUNCTION, &seekData);
        option(CURLOPT_SEEKDATA, &io);
        option(CURLOPT_INFILESIZE_LARGE, curl_off_t(input.size()));
    } else if (r.operation == TransferOperation::List) {
        if (r.url.scheme().startsWith("webdav")) {
            option(CURLOPT_CUSTOMREQUEST, "PROPFIND");
            headers = curl_slist_append(headers, "Depth: 1");
        } else option(CURLOPT_DIRLISTONLY, 1L);
    }
    if (headers) option(CURLOPT_HTTPHEADER, headers);
    CURLcode code = setup;
    if (setup == CURLE_OK && curl_multi_add_handle(multi, easy) == CURLM_OK) {
        int running = 1;
        CURLMcode mc = CURLM_OK;
        while (running && !job.cancelled) {
            mc = curl_multi_perform(multi, &running);
            if (mc != CURLM_OK || !running) break;
            mc = curl_multi_poll(multi, nullptr, 0, 100, nullptr);
            if (mc != CURLM_OK) break;
        }
        code = CURLE_FAILED_INIT;
        int queued = 0;
        while (auto *message = curl_multi_info_read(multi, &queued))
            if (message->msg == CURLMSG_DONE) code = message->data.result;
        if (job.cancelled) code = CURLE_ABORTED_BY_CALLBACK;
        curl_multi_remove_handle(multi, easy);
    } else if (code == CURLE_OK) code = CURLE_FAILED_INIT;
    long response = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response);
    result.responseCode = int(response);
    long httpVersion = 0;
    curl_easy_getinfo(easy, CURLINFO_HTTP_VERSION, &httpVersion);
    if (httpVersion == CURL_HTTP_VERSION_1_0) result.httpVersion = "1.0";
    else if (httpVersion == CURL_HTTP_VERSION_1_1) result.httpVersion = "1.1";
    else if (httpVersion == CURL_HTTP_VERSION_2_0) result.httpVersion = "2";
    else if (httpVersion == CURL_HTTP_VERSION_3) result.httpVersion = "3";
    curl_easy_cleanup(easy);
    curl_multi_cleanup(multi);
    curl_slist_free_all(headers);
    result.bytes = io.bytes;
    result.sha256 = io.hash.result().toHex();
    result.cancelled = job.cancelled;
    if (result.cancelled) result.error = "cancelled";
    else if (!io.error.isEmpty()) result.error = io.error;
    else if (code == CURLE_FILESIZE_EXCEEDED) result.error = "size_limit_exceeded";
    else if (code == CURLE_OPERATION_TIMEDOUT) result.error = "timeout";
    else if (code != CURLE_OK) result.error = "transfer_failed";
    else if ((url.scheme() == "http" || url.scheme() == "https") && response >= 300 && response < 400)
        result.error = "redirect_requires_new_request";
    else if (!r.expectedSha256.isEmpty() && result.sha256 != r.expectedSha256.toLower()) result.error = "checksum_mismatch";
    else if (!io.uploading && !output.commit()) result.error = "local_commit_failed";
    else result.ok = true;
    return result;
}
#endif
} // namespace

struct FileTransfer::Impl { QHash<QString, std::shared_ptr<Job>> jobs; };

FileTransfer::FileTransfer(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {
    qRegisterMetaType<TransferResult>();
}
FileTransfer::~FileTransfer() {
    for (auto &job : d->jobs) job->cancelled = true;
    for (auto &job : d->jobs) {
        disconnect(job->thread, nullptr, this, nullptr);
        job->thread->wait();
        delete job->thread;
    }
}
QString FileTransfer::backendVersion() {
#ifdef IISERVERHOST_CURL
    if (curlReady()) return QString::fromLatin1(curl_version());
#endif
    return {};
}
QStringList FileTransfer::httpVersions() {
    QStringList result;
#ifdef IISERVERHOST_CURL
    if (!curlReady()) return result;
    bool http = false;
    for (const auto &p : protocols()) if (p.scheme == "http" || p.scheme == "https") http = true;
    if (!http) return result;
    result = {"1.0", "1.1"};
    const auto features = curl_version_info(CURLVERSION_NOW)->features;
    if (features & CURL_VERSION_HTTP2) result.append("2");
    if (features & CURL_VERSION_HTTP3) result.append("3");
#endif
    return result;
}
QList<TransferProtocol> FileTransfer::protocols() {
    QList<TransferProtocol> result;
#ifdef IISERVERHOST_CURL
    if (!curlReady()) return result;
    const QList<TransferProtocol> candidates{
        {"file", true, true, false, false}, {"http", true, true, false, false},
        {"https", true, true, false, true}, {"webdav", true, true, true, false},
        {"webdavs", true, true, true, true}, {"ftp", true, true, true, false},
        {"ftps", true, true, true, true}, {"ftpes", true, true, true, true},
        {"sftp", true, true, true, true}, {"scp", true, true, false, true},
        {"tftp", true, true, false, false}, {"smb", true, true, false, false},
        {"smbs", true, true, false, true}, {"gopher", true, false, false, false},
        {"gophers", true, false, false, true}
    };
    const auto *version = curl_version_info(CURLVERSION_NOW);
    for (const auto &candidate : candidates) {
        for (const char *const *p = version->protocols; p && *p; ++p) {
            if (wireScheme(candidate.scheme) == QString::fromLatin1(*p)
                && (candidate.scheme != "ftpes" || (version->features & CURL_VERSION_SSL))) {
                result.append(candidate); break;
            }
        }
    }
#endif
    return result;
}
QString FileTransfer::start(const TransferRequest &request) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
#ifdef IISERVERHOST_CURL
    error = curlReady() ? validate(request) : "backend_unavailable";
#else
    error = "backend_unavailable";
#endif
    if (error.isEmpty() && d->jobs.size() >= 4) error = "too_many_transfers";
    if (!error.isEmpty()) {
        TransferResult result; result.error = error; result.protocol = request.url.scheme();
        QTimer::singleShot(0, this, [this, id, result] { emit finished(id, result); });
        return id;
    }
#ifdef IISERVERHOST_CURL
    auto job = std::make_shared<Job>();
    job->thread = QThread::create([this, id, request, job] {
        job->result = perform(request, *job, [this, id](qint64 bytes, qint64 total) {
            QMetaObject::invokeMethod(this, [this, id, bytes, total] { emit progress(id, bytes, total); }, Qt::QueuedConnection);
        });
    });
    connect(job->thread, &QThread::finished, this, [this, id, job] {
        d->jobs.remove(id);
        job->thread->deleteLater();
        emit finished(id, job->result);
    });
    d->jobs.insert(id, job);
    job->thread->start();
#endif
    return id;
}
void FileTransfer::cancel(const QString &id) { if (const auto job = d->jobs.value(id)) job->cancelled = true; }
int FileTransfer::activeCount() const { return int(d->jobs.size()); }

} // namespace iiServerHost

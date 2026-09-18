#include "StorageBridge.h"
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#ifdef IISERVERHOST_STORAGE_PROCESS
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>

namespace iiServerHost {
namespace {
bool privatePath(const QString &path, bool directory) {
    const QFileInfo f(path);
    if (!f.isAbsolute() || f.isSymLink() || f.canonicalFilePath() != QDir::cleanPath(path)
        || (directory ? !f.isDir() : !f.isFile())) return false;
#ifdef Q_OS_UNIX
    const auto other = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    if (f.permissions() & other) return false;
#endif
    return f.isReadable();
}
QString executable(const StorageBridgeOptions &o) {
    if (o.executable.isEmpty()) return QStandardPaths::findExecutable("rclone");
    const QFileInfo f(o.executable);
    return f.isAbsolute() && f.isFile() && f.isExecutable() ? f.absoluteFilePath() : QString();
}
QString validateBackend(const StorageBridgeOptions &o, bool requireRclone = true) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    Q_UNUSED(o)
    return "external_backend_unavailable_on_mobile";
#else
    if (requireRclone && executable(o).isEmpty()) return "backend_unavailable";
    if (!privatePath(o.runtimeDirectory, true)) return "private_runtime_directory_required";
    if (!o.configFile.isEmpty() && !privatePath(o.configFile, false)) return "private_config_required";
    if (o.configPassword.contains('\0')) return "invalid_config_password";
    return {};
#endif
}
bool remoteLocation(const QString &path) { return path.startsWith(':') || !QDir::isAbsolutePath(path); }
QString validateLocation(const QString &path, const StorageBridgeOptions &o) {
    if (path.isEmpty() || path.size() > 8192 || path.contains(QChar::Null) || path.contains('\n') || path.contains('\r'))
        return "invalid_location";
    if (!remoteLocation(path)) return {};
    // Only a named remote, not rclone's on-the-fly :backend,option=value: syntax.
    if (!QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_-]*:[^\\x00-\\x1f]*$").match(path).hasMatch()) return "invalid_location";
    if (o.configFile.isEmpty()) return "explicit_config_required";
    return {};
}
QProcessEnvironment environment(const StorageBridgeOptions &o) {
    // Deliberately exclude ambient cloud credentials, rclone flags, auth proxies and config locations.
    QProcessEnvironment env;
    const auto system = QProcessEnvironment::systemEnvironment();
    for (const auto &key : {"PATH", "SystemRoot", "WINDIR", "LANG", "LC_ALL"})
        if (system.contains(key)) env.insert(key, system.value(key));
    env.insert("TMPDIR", o.runtimeDirectory);
    env.insert("TMP", o.runtimeDirectory);
    env.insert("TEMP", o.runtimeDirectory);
    if (!o.configPassword.isEmpty()) env.insert("RCLONE_CONFIG_PASS", QString::fromUtf8(o.configPassword));
    return env;
}
QStringList commonArguments(const StorageBridgeOptions &o) {
    return {"--config", o.configFile.isEmpty() ? QProcess::nullDevice() : o.configFile,
            "--cache-dir", o.runtimeDirectory, "--use-json-log", "--log-level", "ERROR", "--stats", "0",
            "--ask-password=false", "--retries", "1", "--low-level-retries", "1"};
}
struct BridgeJob {
    QProcess *process = nullptr;
    QByteArray output;
    QString failure;
    bool cancelled = false;
    bool completed = false;
    bool json = false;
};
bool inside(const QString &path, const QString &root) {
    const auto clean = QDir::cleanPath(path);
    return clean == root || clean.startsWith(root + '/');
}
struct ServerSpec { QString command; bool tls; bool auth; bool experimental; };
ServerSpec serverSpec(FileServerProtocol protocol) {
    switch (protocol) {
    case FileServerProtocol::Http: return {"http", false, true, false};
    case FileServerProtocol::Https: return {"http", true, true, false};
    case FileServerProtocol::WebDav: return {"webdav", false, true, false};
    case FileServerProtocol::WebDavs: return {"webdav", true, true, false};
    case FileServerProtocol::Ftp: return {"ftp", false, true, false};
    case FileServerProtocol::Ftps: return {"ftps", true, true, false};
    case FileServerProtocol::FtpES: return {"ftpes", true, true, false};
    case FileServerProtocol::Sftp: return {"sftp", false, true, false};
    case FileServerProtocol::S3: return {"s3", false, true, true};
    case FileServerProtocol::S3Tls: return {"s3", true, true, true};
    case FileServerProtocol::Restic: return {"restic", false, true, false};
    case FileServerProtocol::ResticTls: return {"restic", true, true, false};
    case FileServerProtocol::Nfs: return {"nfs", false, false, false};
    }
    return {};
}
QString validateServer(const FileServerOptions &o, const StorageBridgeOptions &backend) {
    const auto spec = serverSpec(o.protocol);
    const bool pythonFtp = spec.command == "ftps" || spec.command == "ftpes";
    auto error = validateBackend(backend, !pythonFtp);
    if (!error.isEmpty()) return error;
    error = validateLocation(o.root, backend);
    if (!error.isEmpty()) return error;
    if (!remoteLocation(o.root)) {
        QFileInfo root(o.root);
        if (!root.isDir() || root.isSymLink() || root.canonicalFilePath() != QDir::cleanPath(o.root)) return "invalid_root";
        if (inside(backend.runtimeDirectory, root.canonicalFilePath())
            || (!backend.configFile.isEmpty() && inside(backend.configFile, root.canonicalFilePath()))
            || (!o.privateKey.isEmpty() && inside(o.privateKey, root.canonicalFilePath()))) return "secrets_inside_exported_root";
    }
    if (!o.port || o.address.isNull() || o.address.isMulticast()) return "invalid_listen_address";
    if (spec.command.isEmpty()) return "unsupported_protocol";
    if (spec.auth && (o.username.isEmpty() || (o.password.isEmpty() && !(spec.command == "sftp" && !o.authorizedKeys.isEmpty()))))
        return "authentication_required";
    if (o.username.contains(':') || o.username.contains(',') || o.username.contains('"') || o.username.contains(QChar::Null)
        || o.password.contains('\0') || o.username.contains('\n') || o.username.contains('\r')) return "invalid_credentials";
    if (!spec.tls && spec.command != "sftp" && !o.address.isLoopback() && !o.allowCleartext) return "cleartext_disabled";
    if (spec.tls && (!QFileInfo(o.certificate).isAbsolute() || !QFileInfo(o.certificate).isFile()
        || !QFileInfo(o.certificate).isReadable() || !privatePath(o.privateKey, false))) return "tls_identity_required";
    if (spec.command == "sftp" && !privatePath(o.privateKey, false)) return "ssh_host_key_required";
    if (spec.command == "sftp" && !o.authorizedKeys.isEmpty() && (!QFileInfo(o.authorizedKeys).isAbsolute()
        || !QFileInfo(o.authorizedKeys).isFile() || !QFileInfo(o.authorizedKeys).isReadable())) return "invalid_authorized_keys";
    if (spec.command == "restic" && o.readOnly) return "readonly_not_supported_by_restic";
    if (spec.command == "ftp" || pythonFtp) {
        const auto range = QRegularExpression("^([0-9]{1,5})-([0-9]{1,5})$").match(o.passivePorts);
        if (!range.hasMatch() || range.captured(1).toInt() < 1 || range.captured(2).toInt() > 65535
            || range.captured(1).toInt() > range.captured(2).toInt()) return "invalid_passive_ports";
        if (!o.publicAddress.isEmpty() && QHostAddress(o.publicAddress).isNull()) return "invalid_public_address";
    }
    if (pythonFtp) {
        const QFileInfo python(backend.ftpPythonExecutable), script(backend.ftpServerScript);
        if (!python.isAbsolute() || !python.isFile() || !python.isExecutable()
            || !script.isAbsolute() || !script.isFile()) return "ftp_tls_backend_unavailable";
        if (remoteLocation(o.root)) return "ftp_tls_requires_local_root";
        if (inside(script.absoluteFilePath(), QFileInfo(o.root).canonicalFilePath())) return "server_script_inside_exported_root";
    }
    return {};
}
} // namespace

struct StorageBridge::Impl {
    StorageBridgeOptions options;
    QHash<QString, std::shared_ptr<BridgeJob>> jobs;
};
StorageBridge::StorageBridge(StorageBridgeOptions options, QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {
    d->options = std::move(options);
    qRegisterMetaType<StorageResult>();
}
StorageBridge::~StorageBridge() {
    const auto jobs = d->jobs;
    for (auto &job : jobs) {
        disconnect(job->process, nullptr, this, nullptr);
        job->process->kill();
    }
    for (auto &job : jobs) job->process->waitForFinished(3000);
}
QString StorageBridge::start(const StorageRequest &request) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error = validateBackend(d->options);
    QStringList args;
    bool json = false;
    if (error.isEmpty() && request.operation != StorageOperation::Providers) error = validateLocation(request.source, d->options);
    if (error.isEmpty() && (request.operation == StorageOperation::CopyFile || request.operation == StorageOperation::CopyDirectory
        || request.operation == StorageOperation::Check)) error = validateLocation(request.destination, d->options);
    if (error.isEmpty() && request.timeoutMs <= 0) error = "invalid_timeout";
    if (error.isEmpty() && d->jobs.size() >= 4) error = "too_many_transfers";
    switch (request.operation) {
    case StorageOperation::Providers: args = {"config", "providers"}; json = true; break;
    case StorageOperation::List: args = {"lsjson", request.source}; json = true; break;
    case StorageOperation::Stat: args = {"lsjson", request.source, "--stat"}; json = true; break;
    case StorageOperation::CopyFile: args = {"copyto", request.source, request.destination}; break;
    case StorageOperation::CopyDirectory: args = {"copy", request.source, request.destination}; break;
    case StorageOperation::MakeDirectory: args = {"mkdir", request.source}; break;
    case StorageOperation::RemoveFile: args = {"deletefile", request.source}; break;
    case StorageOperation::Check: args = {"check", request.source, request.destination, "--download"}; break;
    default: error = "unsupported_operation";
    }
    if (request.operation == StorageOperation::CopyFile || request.operation == StorageOperation::CopyDirectory) {
        // copyto in rclone 1.75.1 does not enforce --immutable for an existing file.
        // --ignore-existing is required for the documented skip-existing policy.
        if (!request.overwrite) args.append("--ignore-existing");
        args.append("--checksum");
        args.append("--local-no-set-modtime");
    }
    if (!error.isEmpty()) {
        StorageResult result; result.error = error;
        QTimer::singleShot(0, this, [this, id, result] { emit finished(id, result); });
        return id;
    }
    auto job = std::make_shared<BridgeJob>();
    auto *process = new QProcess(this);
    job->process = process; job->json = json;
    auto *timer = new QTimer(process); timer->setSingleShot(true);
    auto finish = [this, job, id, timer](int code, QProcess::ExitStatus status) {
        if (job->completed) return;
        job->completed = true; timer->stop();
        StorageResult result;
        result.cancelled = job->cancelled; result.exitCode = code;
        if (job->cancelled) result.error = "cancelled";
        else if (!job->failure.isEmpty()) result.error = job->failure;
        else if (status != QProcess::NormalExit || code != 0) result.error = "backend_failed";
        else if (job->json) {
            QJsonParseError parse;
            const auto document = QJsonDocument::fromJson(job->output, &parse);
            if (parse.error != QJsonParseError::NoError) result.error = "invalid_backend_response";
            else if (document.isArray()) result.data = document.array();
            else if (document.isObject()) result.data = document.object();
        }
        result.ok = result.error.isEmpty();
        d->jobs.remove(id);
        job->process->deleteLater();
        emit finished(id, result);
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [job] {
        const auto bytes = job->process->readAllStandardOutput();
        if (!job->json) return;
        if (job->output.size() + bytes.size() > 16 * 1024 * 1024) {
            job->failure = "result_size_limit_exceeded"; job->process->kill(); return;
        }
        job->output += bytes;
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::finished, this, finish);
    connect(process, &QProcess::errorOccurred, this, [job, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { job->failure = "backend_start_failed"; finish(-1, QProcess::CrashExit); }
    });
    connect(timer, &QTimer::timeout, this, [job] { job->failure = "timeout"; job->process->kill(); });
    d->jobs.insert(id, job);
    process->setProcessEnvironment(environment(d->options));
    process->setWorkingDirectory(d->options.runtimeDirectory);
    process->setStandardInputFile(QProcess::nullDevice());
    process->start(executable(d->options), args + commonArguments(d->options));
    timer->start(request.timeoutMs);
    return id;
}
void StorageBridge::cancel(const QString &id) {
    if (const auto job = d->jobs.value(id)) { job->cancelled = true; job->process->kill(); }
}

struct FileProtocolServer::Impl {
    StorageBridgeOptions backend;
    QProcess *process = nullptr;
    QString error;
    bool stopping = false;
};
FileProtocolServer::FileProtocolServer(StorageBridgeOptions backend, QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {
    d->backend = std::move(backend);
}
FileProtocolServer::~FileProtocolServer() {
    if (d->process) {
        disconnect(d->process, nullptr, this, nullptr);
        d->process->kill(); d->process->waitForFinished(3000);
    }
}
bool FileProtocolServer::start(const FileServerOptions &options) {
    if (d->process) { d->error = "already_running"; return false; }
    d->error = validateServer(options, d->backend);
    if (!d->error.isEmpty()) return false;
    d->stopping = false;
    const auto spec = serverSpec(options.protocol);
    const bool pythonFtp = spec.command == "ftps" || spec.command == "ftpes";
    QString address = options.address.toString();
    if (options.address.protocol() == QAbstractSocket::IPv6Protocol) address = '[' + address + ']';
    QStringList args{"serve", spec.command, options.root, "--addr", address + ':' + QString::number(options.port)};
    if (options.readOnly) args.append("--read-only");
    if (spec.tls && !pythonFtp) {
        args += {"--cert", options.certificate, "--key", options.privateKey};
        if (spec.command != "ftp") args += {"--min-tls-version", "tls1.2"};
    }
    if (spec.command == "sftp") args += {"--key", options.privateKey, "--authorized-keys",
        options.authorizedKeys.isEmpty() ? QProcess::nullDevice() : options.authorizedKeys};
    if (spec.command == "nfs" && !options.readOnly)
        args += {"--vfs-cache-mode", "full", "--vfs-write-back", "0s"};
    if (spec.command == "ftp") {
        args += {"--passive-port", options.passivePorts};
        if (!options.publicAddress.isEmpty()) args += {"--public-ip", options.publicAddress};
    }
    auto env = environment(d->backend);
    if (spec.command == "s3") {
        auto pair = options.username + ',' + QString::fromUtf8(options.password);
        pair.replace('"', "\"\"");
        env.insert("RCLONE_AUTH_KEY", '"' + pair + '"');
    } else if (spec.auth && !pythonFtp) {
        env.insert("RCLONE_USER", options.username);
        if (!options.password.isEmpty()) env.insert("RCLONE_PASS", QString::fromUtf8(options.password));
    }
    // Never enable symlink following or user-supplied auth-proxy commands.
    if (pythonFtp) {
        args = {"-B", d->backend.ftpServerScript, "--root", options.root, "--address", options.address.toString(),
            "--port", QString::number(options.port), "--certificate", options.certificate, "--key", options.privateKey,
            "--passive-ports", options.passivePorts};
        if (spec.command == "ftps") args.append("--implicit");
        if (options.readOnly) args.append("--read-only");
        if (!options.publicAddress.isEmpty()) args += {"--public-address", options.publicAddress};
        env.insert("IISERVERHOST_FTP_USER", options.username);
        env.insert("IISERVERHOST_FTP_PASSWORD", QString::fromUtf8(options.password));
    } else args += commonArguments(d->backend);
    auto *process = new QProcess(this); d->process = process;
    connect(process, &QProcess::readyReadStandardOutput, this, [process] { process->readAllStandardOutput(); });
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::started, this, &FileProtocolServer::started);
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        d->process = nullptr;
        if (!d->stopping) { d->error = "server_exited"; emit failed(d->error); }
        process->deleteLater(); emit stopped();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            d->error = "backend_start_failed"; d->process = nullptr;
            process->deleteLater(); emit failed(d->error); emit stopped();
        }
    });
    process->setProcessEnvironment(env);
    process->setWorkingDirectory(d->backend.runtimeDirectory);
    process->setStandardInputFile(QProcess::nullDevice());
    process->start(pythonFtp ? d->backend.ftpPythonExecutable : executable(d->backend), args);
    return true;
}
void FileProtocolServer::stop() {
    if (!d->process) return;
    d->stopping = true;
    auto *process = d->process;
    process->terminate();
    QTimer::singleShot(2000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}
bool FileProtocolServer::isRunning() const { return d->process && d->process->state() == QProcess::Running; }
QString FileProtocolServer::errorString() const { return d->error; }

} // namespace iiServerHost
#else
// QProcess is absent from several mobile Qt builds. Keep the public ABI available
// without including or linking that optional Qt feature.
namespace iiServerHost {
struct StorageBridge::Impl {};
StorageBridge::StorageBridge(StorageBridgeOptions, QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {
    qRegisterMetaType<StorageResult>();
}
StorageBridge::~StorageBridge() = default;
QString StorageBridge::start(const StorageRequest &) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTimer::singleShot(0, this, [this, id] {
        StorageResult r; r.error = "external_backend_disabled"; emit finished(id, r);
    });
    return id;
}
void StorageBridge::cancel(const QString &) {}
struct FileProtocolServer::Impl {};
FileProtocolServer::FileProtocolServer(StorageBridgeOptions, QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}
FileProtocolServer::~FileProtocolServer() = default;
bool FileProtocolServer::start(const FileServerOptions &) { return false; }
void FileProtocolServer::stop() {}
bool FileProtocolServer::isRunning() const { return false; }
QString FileProtocolServer::errorString() const { return "external_backend_disabled"; }
} // namespace iiServerHost
#endif

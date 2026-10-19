# iiServerHost

Package release: **0.6.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


## File presentation ownership

FileShare uses iiFileProvider 0.7's control-file rule for both native path access
and enumeration. Preview/classification companions, private provider catalogs,
staging and locks are excluded before pagination; ordinary public hidden files
remain shareable. A directory with hundreds of derived previews therefore does
not consume a page before its user assets can be listed. The hosting regression
checks this boundary and preservation of access to original files.
Completed native upload publication also notifies the provider, including when
the shared directory is outside a monitored Society drive.

<a id="060-lan-파일-본문의-바이너리-전송"></a>

## 0.6.0: Binary transmission of LAN  file body content

`LanPeer` 's TLS  pairing negotiates `binary=1` . If both sides support it, only file `data`  is transmitted as 256   KiB  or less raw binary body. The client's negotiation result is confirmed with `binaryTransferEnabled()` . Other control messages, legacy clients, and relay paths maintain JSON, and the existing request handler API is also maintained. The network bytes that were increasing with Base64  encoding of the file body are reduced by about 25% . It is not even removing the encoding cost of the internal JSON  API.

The frame is `ISB1` , big-endian 32-bit JSON  header length, up to 64   KiB  header, up to 256   KiB  file bytes order. The header contains allowed file field positions and the rest of the original message. Length, position, duplicate fields, and negotiation status are validated, and binary requests before pairing are rejected. `iiServerHost.lan`  checks actual TLS  bidirectional transmission, frame upper limit, and compatibility with previous JSON  peer. TCP  is the WebSocket  transmission and QUIC  is not implemented.

<a id="050-표준-파일-전송nas클라우드서버-호스팅"></a>

## 0.5.0: Standard file transfer · NAS · Cloud · Server hosting

`FileTransfer`  transmits based on libcurl-based HTTP (S), WebDAV(S) , FTP / FTPS / FTPES , SFTP / SCP , TFTP , Gopher(s), and local files. `StorageBridge`  provides common file operations across NAS · Cloud · Virtual storage configured in rclone. `FileProtocolServer`  manages HTTP (S), WebDAV(S) , FTP / FTPS / FTPES , SFTP , S3 , NFSv3, and restic servers. It includes binary streaming, atomic replacement of downloads · SHA-256 , job cancellation, credential separation, and actual engine feature lookup. Existing Peer · LanPeer  API is maintained.

[support matrix, C++   API , command-line execution, and dependencies and validation](docs/FileTransfer.md)are referenced. Supported features depend on the actual libcurl/rclone build and each backend, and it does not mean guaranteeing all features of all services.

<a id="041-탐색한-기기의-직접-lan-페어링"></a>

## 0.4.1: direct LAN pairing of the scanned device

`createDeviceOffer(peerId)` creates one-time connection information bound to the device selected through discovery. After `join()`, `phase()` on both sides becomes `confirming`, and `verificationCode()` displays the same 12-digit hex code in 4-4-4 format. No Files list or bytes are returned before the host calls `confirmDevice()`. The code is bound to the SHA-256 of the host's TLS certificate fingerprint, one-time key, client random nonce, and device ID. Another device ID, duplicate connections, and expired or canceled requests are rejected. Confirmation waits for at most 60 seconds. `pairedDeviceIds()` provides currently connected devices on both host and client, allowing connection state to appear in the list. Discovery information is not proof of trust; the consumer UI must compare the codes on both sides and receive explicit connection confirmation from the host.

The SDK does not own Bonjour/NSD or account login. The Society app is responsible for device discovery, selection, and invitation reception within the account scope, and the SDK reuses existing TLS ·Files connections. `iiServerHost.lan` checks for selected device binding, prohibits calling the Files handler before final confirmation, checks for code match, and inspects confirmation and cancellation. Login tests are not called.

<a id="직접-lan-qr-페어링"></a>

## Directly LAN QR pairing

`LanPeer` connects desktop and mobile directly without an external relay or account server. The desktop's `startHost()` and `createOffer()` generate a version-2 `LanLink` QR containing a private IPv4 address, TLS certificate SHA-256, a 256-bit one-time key, and a 60-second expiry. Mobile connects through `join(qr, deviceId, name)`. No application data is sent before the QR certificate fingerprint is verified, and no login cookies or account models are received. Scanning the QR itself constitutes the host's access authorization.

After checking the Files list and completing the client confirm, send `paired` to both sides and allow `request()`. Cancel, expire, reuse, and other fingerprints are rejected. The connection key is stored in memory only and reconnected with a new QR after disconnection. Completed connections are not terminated via `cancelPairing()`. `stop()` closes the listener, connection, and pending requests. The SDK's existing RelayServer /Peer and version 1 PairingLink are maintained as native consumer compatible APIs.

The host is built only on desktop platforms. iOS/Android reject `startHost()` and include only the client. It reuses Qt 6.8.3 Core/Network/WebSockets, and desktop platforms use OpenSSL 3 Crypto to generate temporary RSA-2048 certificates. OpenSSL is Apache 2.0, and notices are provided in `ThirdParty/OpenSSL-LICENSE.txt` and the installed `share/iiServerHost/licenses/`. It uses the RSA key format whose hosting compatibility with Qt Secure Transport has been verified. Private keys are not stored long-term in files or the keychain. It follows [OpenSSL keygen](https://docs.openssl.org/3.5/man3/EVP_PKEY_keygen/), [X509_sign](https://docs.openssl.org/3.5/man3/X509_sign/), and [QWebSocket TLS](https://doc.qt.io/qt-6.8/qwebsocket.html).

QR addresses permit only RFC 1918 IPv4 and explicitly specified test loopback. Automatic discovery prioritizes Wi-Fi/Ethernet and excludes public addresses, DNS, link-local metadata, and VPN point-to-point interfaces. It attempts at most 8 addresses in order, with no internet fallback. IPv6-only LANs are unsupported. The host applies TLS, a 1 MiB message limit, a4 MiB outbound-queue limit, and a limit of 8 connections. The client limits concurrent requests to 32.

`iiServerHost.lan` validates actual TLS pairing, file bytes, expiration, cancel, reuse, and fingerprint mismatch without calling login or SessionAuthenticator. The same check is applied to install consumers as well.


A local and remote app-hosting SDK based on C++20 / Qt 6.8.3, version 0.5.0. A consumer app uses a single `Peer` to provide its own files while accessing other hosts under the same account. Nearby devices first use a local TLS connection, switching to a remote WebSocket relay if the address cannot be reached. Both devices connect outbound to the relay, so router port forwarding is unnecessary.

<a id="구성과-신뢰-경계"></a>

## Composition and trust boundary

- `RelayServer`: List of devices per authenticated account and service, one-time local access token, and request relay. It does not store files. It isolates lists and requests from different accounts.
- `SessionAuthenticator`: Sends a cookie to the fixed HTTPS `/Account/GraphQL` and reads the verified `account.sub`. The `accountId` display string or a client-declared account is not authorization proof. It does not use response cookies, cache, or redirects.
- `Peer`: Handles host registration, device search, local-priority transfer, remote switch, exponential reconnection, and request timeout. It displays only hosts of the same `service`.
- `FileShare`: Includes list, metadata, chunked read, new file creation, and directory creation under root. The application can connect to other storage via `RequestHandler`. iiServerHost does not reference Society. The reserved `.iisacc-temporary.lock` sentinel is excluded from listings and cannot be read, modified, or created through this public API.

Authentication cookies are delivered only to trusted relays and account verification servers. Other devices receive 256-bit random one-time access tokens. Device local certificates SHA-256 are obtained from the trusted relay list and compared against the actual TLS certificate. Certificate errors are never ignored unconditionally. Relays authenticate using the default trusted CA. Non-TLS URLs and listeners are allowed only on numeric loopback addresses.

Account verification is refreshed every 30 seconds, and the permission lifetime is a maximum of 60 seconds. Access is revoked upon refresh failure, expiration, logout, or connection termination. Local access tokens are consumed one time times and the corresponding connection is terminated upon expiration. LAN file transfers also require **device discovery and account verification relay connections**. QR pairing allows selecting hosts of the same account, but pairing does not replace account authentication or grant offline access permissions.

<a id="소비-앱-api"></a>

## Consumer app API

```cpp
#include <iiServerHost.h>

iiServerHost::FileShare files("/absolute/path/to/shared/Files");
iiServerHost::Peer peer;
iiServerHost::PeerOptions options;
options.relayUrl = QUrl("wss://your-relay.example/server-host");
options.credential = authenticatedCookieHeader; // Do not display this on screen or store it on disk.
options.peerId = stableDeviceId;
options.name = "My desktop";
options.service = "com.example.files";
options.localTls = hostCertificateAndPrivateKey;
peer.start(options, [&files](const QString &, const QJsonObject &request) {
    return files.handle(request);
});
QObject::connect(&peer, &iiServerHost::Peer::completed,
    [](QString id, QJsonObject result, QString transport) { /* Handle the result */ });
const QString id = peer.request(otherDeviceId, {{"op", "list"}, {"path", ""}});
```

`PeerOptions.localEnabled=false` is remote-only. `localHostingEnabled=false` disables only the local listener while retaining local-first access to other devices. `hostFiles=false` is a client that performs discovery and access only. If no local address is specified, active IPv4 interfaces are registered, and only addresses in the same subnet are attempted. `localTimeoutMs` defaults to 1.2 seconds per address; the overall request limit is 15 seconds. An API can explicitly specify an IPv6 address, but automatic advertisement uses IPv4.

Handlers and signals are executed in the owning Qt thread. Large tasks must be separated into the application's asynchronous storage layer. Requests already sent are not re-executed via path switching. If a write response breaks, the caller must check the results as a list.

<a id="qr-페어링"></a>

## QR pairing

Since the relay URL in QR is nested-encoded, escapes such as `%2F` · `%25` remain unchanged. This case is also verified via creation and parsing regression tests.

`PairingLink` is responsible for the creation and strict parsing of `society://pair?v=1&relay=...&host=...&code=...`. It checks version, field duplication, host ID, 256-bit code, and the relay's TLS URL, and does not make network requests. The consumer app must compare the relay contained in the code against independent trusted settings. Sending the account cookie to that server solely based on parsing success is not allowed.

| Peer API |Role|
| --- | --- |
| `createPairingOffer()` |The connected host issues a one-time code. Events are distinguished by the returned request ID.|
| `claimPairingOffer(code, expectedHost)` |The connected client submits the QR code and host ID.|
| `confirmPairing(pairingId)` |The client confirms connection completion after actual host access verification.|
| `cancelPairing(requestId)` |Cancel its own request. Already consumed QR codes cannot be recovered.|
| `pairingEvent(QJsonObject)` |Receives `id`, `status`, and data for each stage.|

Wire requests are `pair-offer`, `pair-claim`, `pair-confirm`, and `pair-cancel`, and the response is `type: pairing`. Status is `offered`, `claimed`, `paired`, `error`, `cancelled`, and `expired`. `offered` includes `code`, `peerId`, `name`, and string epoch-millisecond `expires`. `claimed` passes `pairingId`, relative `peerId` · `name`, and `expires` to both sides. The `paired` is delivered to both sides only if that client's confirm is received. SDK The server itself does not determine the meaning of file list inspection, and after receiving the Files list response where `DevicePairing` succeeded, it sends a confirm.

The code is a hex representation of 32 bytes generated by the OS random generator. Server memory stores only the code's SHA-256. Its lifetime is the shorter of 60 seconds and the host authentication's remaining lifetime when issued. It is consumed immediately on claim. Confirmation waits at most 15 seconds and never beyond the QR expiry. Only registered hosts and clients for the same account and service connect. Another account, another host ID, and reuse are rejected with the same error. Connection closure, authentication failure, host unregistration, reissuance, cancellation, or expiry discards the corresponding pending state. Issuance and claim attempts are limited per session to 6 per 30 seconds. The server does not store persistent device relationships or account cookies on disk.

`iiServerHost.pairing` strictly checks QR parsing, account isolation, host matching, one-time use, blocking another client's confirm, reissuance, cancellation, connection termination, expiration, and attempt limits as actual WebSocket. The install consumer also performs the same pairing check. Existing files, local/remote transfer APIs, and `helloWorld()` are maintained.

<a id="파일-계약"></a>

## File Contract

| op |Request Fields|Response|
| --- | --- | --- |
| `list` |`path`, optional string `cursor`|Maximum 256 `entries`, `nextCursor`|
| `stat` | `path` |String `size`, opaque `version`|
| `read` |`path`, string `offset`, optional `version`|Maximum 256 KiB base64 `data`, `size`, `offset`, `version`, `eof`|
| `write` | `path`, base64 `data` |Atomically create new files only, maximum 256 KiB, preserve existing files|
| `mkdir` | `path` |New directory under existing parent|

Integer size and offset are passed as strings to avoid JSON floating-point loss. Downloads must first receive `stat`, then all chunks with the same `version`. Modified files fail with `file_changed`. Directory page cursors must restart when the directory changes. Delete, overwrite, large upload, synchronization conflict resolution, and OS mount are not included in this API.

Unix file provision uses `openat`, `O_NOFOLLOW`, directory FD, and inode/device checks. Rejects symbolic links, parent path moves, redirections, and replaced roots. New files are published via `linkat` after completing a temporary file in the same directory, so they do not overwrite existing files. Applications also check contracts such as root guard Society UUID. On Windows, requests are rejected until a safe native handle implementation is available by default `FileShare`. Transfer and relay layers and custom handlers are separated based on Qt.

Message 1 MiB, send queue 4 MiB, 128 connections, concurrent requests per device 32, total relay requests 1024, limited to 64 local connections. Relay operators can view file traffic. Each connection to the relay is TLS, but the end-to-end encryption store is not. No account cookies or file data are logged to the relay.

<a id="중계-실행"></a>

## Run relay

```sh
build/ii-server-relay --address 0.0.0.0 --port 9443 \
  --certificate /secure/fullchain.pem --key /secure/private-key.pem \
  --session-url https://iisacc.com/Account/GraphQL
```

When using an existing HTTPS reverse proxy, bind the relay to `127.0.0.1:9443` and expose only WSS externally. The proxy supports WebSocket upgrade and connection lifetime must be longer than 60 seconds. Consumer apps must set the public relay URL. Implementation and loopback verification in this repository are not evidence of deployment on public servers or execution in other physical networks. Do not create new paid infrastructure.

<a id="빌드설치"></a>

## Build and Install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/Volumes/Storage/Qt/6.8.3/macos \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
cmake -S tests/consumer -B build/consumer/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/install;/Volumes/Storage/Qt/6.8.3/macos"
cmake --build build/consumer/build --parallel 4
ctest --test-dir build/consumer/build --output-on-failure
```

It runs the same verification as `INSTALL_PREFIX=... ./install.sh`. The install target is `iiServerHost::iiServerHost`, the public headers are `src/iiServerHost.h`, `src/ServerHost.h`, `src/PairingLink.h`, and `src/iiServerHostExport.h`. It does not create circular references between headers. It uses iOS and `BUILD_SHARED_LIBS=OFF` in `BUILD_TESTING=OFF`. It maintains the existing `helloWorld()` ABI. All build artifacts are placed under `build/`.

<a id="의존성-검토"></a>

## Dependency Review

Only Network and WebSockets were added to the existing Qt 6.8.3 Core. Maintained Qt network, TLS, and WebSocket implementations are used; no custom cryptography or HTTP parser is created. No Qt HTTP Server, WebRTC, separate paid tunnel, or VPN dependency is added. The official terms for Qt WebSockets are commercial or LGPLv3/GPLv2. Actual distribution retains the notices for the selected Qt license and TLS backend. The OpenSSL command line is used to create test certificates and is not a directly linked SDK dependency.

The listener initializes `QT_SSL_USE_TEMPORARY_KEYCHAIN` to `1` so that macOS Secure Transport does not put the server key in the login keychain. It preserves values explicitly set by the application.

References: [Qt WebSockets](https://doc.qt.io/qt-6.8/qtwebsockets-index.html), [QWebSocket message limits](https://doc.qt.io/qt-6.8/qwebsocket.html), and [QSslSocket keychain/trust settings](https://doc.qt.io/qt-6.8/qsslsocket.html).

<a id="라이선스"></a>

## License

SPDX -License-Identifier: AGPL-3.0 -only. Its own code follows [LICENSE](LICENSE). It does not replace the separate licenses of external libraries such as Qt.

`ii-server-host` is a diagnostic executable that uses the same SDK even without a consumer app. It hosts `--root`, `--relay`, `--id`, and `--credential-file`, reads a chunk via `--peer ID --path relative/path --read`, or reads the list without `--read`. It passes LAN and `--certificate` together to the public `--key` listener. It can force a remote path via `--no-local`. The credential file is set to be readable only by the owner and is not included in version control. `iiServerHost.process_hosting` verifies the actual file bytes from both paths via the HTTP authentication verification server and separate relay, host, and client processes.

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.

<a id="계정-graphql-인증"></a>

## Account GraphQL Authentication

`SessionAuthenticator` and `--session-url` receive the GraphQL endpoint. The default is `https://iisacc.com/Account/GraphQL`. The `POST` body is `{"query":"query AccountSession { accountSession { account { sub } } }"}` and only reads `data.accountSession.account.sub` as the authentication subject. It does not authenticate partial responses with errors, null accounts, incorrect Content-Type, or redirects. It updates the relay and server endpoint together.

LAN applications can require mutual account proofs with `setPairingAuthentication`. Proofs bind both peer IDs, pinned certificate, one-use offer and client nonce with separate client/host roles. Authentication precedes Files access. Legacy clients fail closed when callbacks are installed.

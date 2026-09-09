# iiServerHost

## 0.4.0: 직접 LAN 페어링

`LanPeer`는 외부 중계·계정 서버 없이 데스크탑과 모바일을 직접 연결한다. 데스크탑의 `startHost()`와 `createOffer()`가 사설 IPv4 주소·TLS 인증서 SHA-256·256비트 일회용 키·60초 만료를 담은 `LanLink` 버전 2 QR을 생성한다. 모바일은 `join(qr, deviceId, name)`으로 접속한다. QR 인증서 지문 확인 이전에는 응용 데이터를 전송하지 않으며, 로그인 쿠키나 계정 모델을 받지 않는다. QR 스캔 자체가 호스트의 접근 승인이다.

Files 목록 확인과 클라이언트 confirm을 완료한 뒤 양쪽에 `paired`를 보내고 `request()`를 허용한다. 취소·만료·재사용·다른 지문은 거부한다. 연결 키는 메모리에만 보관하며 접속 중단 후에는 새 QR로 재연결한다. 이미 완료된 연결은 `cancelPairing()`으로 종료하지 않는다. `stop()`은 리스너·연결·미완료 요청을 모두 닫는다. SDK의 기존 RelayServer/Peer 및 버전 1 PairingLink는 네이티브 소비자 호환 API로 유지된다.

호스트는 데스크탑에서만 빌드한다. iOS/Android는 `startHost()`를 거부하고 클라이언트만 포함한다. Qt 6.8.3 Core/Network/WebSockets를 재사용하며 데스크탑은 OpenSSL 3 Crypto로 RSA-2048 임시 인증서를 생성한다. OpenSSL은 Apache 2.0이며 `ThirdParty/OpenSSL-LICENSE.txt`와 설치의 `share/iiServerHost/licenses/`에 고지를 제공한다. Qt Secure Transport의 호스팅 호환성을 확인한 RSA 키 형식을 사용한다. 개인 키를 파일이나 키체인에 장기 저장하지 않는다. [OpenSSL keygen](https://docs.openssl.org/3.5/man3/EVP_PKEY_keygen/), [X509_sign](https://docs.openssl.org/3.5/man3/X509_sign/), [QWebSocket TLS](https://doc.qt.io/qt-6.8/qwebsocket.html)를 따른다.

QR 주소는 RFC 1918 IPv4와 명시적인 테스트 loopback만 허용한다. 자동 검색은 Wi-Fi/Ethernet을 우선하고 공인 주소·DNS·link-local metadata·VPN point-to-point 인터페이스를 제외한다. 최대 8개 주소를 순서대로 시도하며 인터넷 fallback은 없다. IPv6 전용 LAN은 지원하지 않는다. 호스트는 TLS와 1 MiB 메시지/4 MiB 송신 대기열, 8개 연결 제한을 적용하며 클라이언트는 동시 요청을 32개로 제한한다.

`iiServerHost.lan`은 로그인이나 SessionAuthenticator를 호출하지 않고 실제 TLS 페어링·파일 바이트·만료·취소·재사용·지문 불일치를 검증한다. 설치 소비자에도 같은 검사를 적용한다.


C++20 / Qt 6.8.3 기반의 로컬·원격 앱 호스팅 SDK, 버전 0.3.0이다. 소비 앱은 `Peer` 하나로 자신의 파일을 제공하면서 같은 계정의 다른 호스트에 접근한다. 가까운 기기는 로컬 TLS 연결을 먼저 사용하고, 해당 주소에 연결할 수 없으면 원격 WebSocket 중계로 전환한다. 양쪽 기기가 중계에 외향 연결하므로 공유기의 포트 포워딩은 필요하지 않다.

## 구성과 신뢰 경계

- `RelayServer`: 인증된 계정·서비스별 기기 목록, 일회용 로컬 접속표, 요청 중계이다. 파일을 저장하지 않는다. 서로 다른 계정의 목록과 요청을 격리한다.
- `SessionAuthenticator`: 고정한 HTTPS `/Account/Session`에 쿠키를 보내 검증된 `account.sub`를 읽는다. `accountId` 표시 문자열이나 클라이언트가 선언한 계정은 권한 증명이 아니다. 응답 쿠키·캐시·리디렉션은 사용하지 않는다.
- `Peer`: 호스트 등록, 기기 검색, 로컬 우선 전송, 원격 전환, 지수형 재접속, 요청 시간 제한을 담당한다. 같은 `service`의 호스트만 표시한다.
- `FileShare`: 루트 아래 목록·메타데이터·분할 읽기·새 파일 생성·디렉터리 생성이다. 애플리케이션은 `RequestHandler`로 다른 저장소를 연결할 수 있다. iiServerHost가 Society를 참조하지 않는다.

인증 쿠키는 신뢰하는 중계와 계정 검증 서버에만 전달된다. 다른 기기에는 256비트 무작위 일회용 접속표를 전달한다. 기기의 로컬 인증서 SHA-256은 인증된 중계 목록에서 얻고 실제 TLS 인증서와 대조한다. 인증서 오류를 무조건 무시하지 않는다. 중계는 기본 신뢰 CA로 인증한다. 비 TLS URL과 리스너는 숫자형 loopback 주소에서만 허용한다.

계정 확인은 30초마다 갱신하며 권한 수명은 최대 60초이다. 갱신 실패·만료·로그아웃·연결 종료 시 접근을 해제한다. 로컬 접속표는 한 번 소비하고 해당 연결도 만료 시 종료한다. LAN 파일 전송에도 **기기 검색과 계정 확인을 위한 중계 연결은 필요하다**. QR 페어링으로 같은 계정의 호스트를 선택할 수 있지만, 페어링이 계정 인증을 대체하거나 오프라인 접근 권한을 부여하지 않는다.

## 소비 앱 API

```cpp
#include <iiServerHost.h>

iiServerHost::FileShare files("/absolute/path/to/shared/Files");
iiServerHost::Peer peer;
iiServerHost::PeerOptions options;
options.relayUrl = QUrl("wss://your-relay.example/server-host");
options.credential = authenticatedCookieHeader; // 화면에 표시하거나 디스크에 저장하지 않는다.
options.peerId = stableDeviceId;
options.name = "My desktop";
options.service = "com.example.files";
options.localTls = hostCertificateAndPrivateKey;
peer.start(options, [&files](const QString &, const QJsonObject &request) {
    return files.handle(request);
});
QObject::connect(&peer, &iiServerHost::Peer::completed,
    [](QString id, QJsonObject result, QString transport) { /* 결과 처리 */ });
const QString id = peer.request(otherDeviceId, {{"op", "list"}, {"path", ""}});
```

`PeerOptions.localEnabled=false`는 원격 전용이다. `localHostingEnabled=false`는 로컬 리스너만 비활성화하고 다른 기기에 대한 로컬 우선 접근을 유지한다. `hostFiles=false`는 검색·접근만 하는 클라이언트이다. 로컬 주소를 지정하지 않으면 활성 IPv4 인터페이스를 등록하고 같은 서브넷 주소만 시도한다. `localTimeoutMs` 기본값은 주소별 1.2초, 전체 요청 제한은 15초이다. IPv6 주소를 명시하는 API는 있지만 자동 광고는 IPv4이다.

핸들러와 신호는 소유 Qt 스레드에서 실행한다. 큰 작업은 애플리케이션의 비동기 저장소 계층으로 분리해야 한다. 이미 전송한 요청은 경로 전환으로 재실행하지 않는다. 쓰기 응답이 끊기면 호출자는 목록으로 결과를 확인해야 한다.

## QR 페어링

QR의 중계 URL은 중첩 인코딩하므로 경로의 `%2F`·`%25` 같은 이스케이프가 변하지 않는다. 이 경우도 생성·파싱 회귀 테스트로 확인한다.

`PairingLink`는 `society://pair?v=1&relay=...&host=...&code=...`의 생성·엄격한 파싱을 담당한다. 버전·필드 중복·호스트 ID·256비트 코드·중계의 TLS URL을 검사하며 네트워크 요청은 하지 않는다. 소비 앱은 코드에 들어 있는 relay를 독립적인 신뢰 설정과 대조해야 한다. 파싱 성공만으로 그 서버에 계정 쿠키를 보내면 안 된다.

| Peer API | 역할 |
| --- | --- |
| `createPairingOffer()` | 연결된 호스트가 일회성 코드를 발급한다. 반환한 요청 ID로 이벤트를 구분한다. |
| `claimPairingOffer(code, expectedHost)` | 연결된 클라이언트가 QR의 코드와 호스트 ID를 제출한다. |
| `confirmPairing(pairingId)` | 클라이언트가 실제 호스트 접근 검사 후 연결 완료를 확인한다. |
| `cancelPairing(requestId)` | 자기 요청을 취소한다. 이미 소모한 QR은 복구되지 않는다. |
| `pairingEvent(QJsonObject)` | `id`, `status`와 단계별 데이터를 받는다. |

wire 요청은 `pair-offer`, `pair-claim`, `pair-confirm`, `pair-cancel`이며 응답은 `type: pairing`이다. 상태는 `offered`, `claimed`, `paired`, `error`, `cancelled`, `expired`이다. `offered`는 `code`, `peerId`, `name`, 문자열 epoch-millisecond `expires`를 포함한다. `claimed`는 `pairingId`, 상대 `peerId`·`name`, `expires`를 양쪽에 전달한다. 그 클라이언트의 confirm을 받은 경우에만 양쪽에 `paired`를 전달한다. SDK 서버 자체가 파일 목록 검사의 의미를 판단하지 않으며, Society의 `DevicePairing`이 성공한 Files 목록 응답을 받은 뒤 confirm을 보낸다.

코드는 OS 무작위 생성기로 만든 32바이트를 hex로 표현한다. 서버 메모리에는 코드의 SHA-256만 보관하며, 수명은 최대 60초와 발급 당시 호스트 인증의 남은 수명 중 짧은 값이다. claim 때 즉시 소모하고 확인 대기는 최대 15초이며 QR 만료를 넘기지 않는다. 같은 계정·같은 서비스의 등록된 호스트와 클라이언트만 연결된다. 다른 계정·다른 호스트 ID·재사용은 동일한 오류로 거부한다. 연결 종료·인증 실패·호스트 해제·재발급·취소·만료 시 해당 대기 상태를 폐기한다. 세션별 발급·claim 시도는 30초당 6회로 제한한다. 서버는 영구 기기 관계나 계정 쿠키를 디스크에 저장하지 않는다.

`iiServerHost.pairing`은 엄격한 QR 파싱, 계정 격리, 호스트 일치, 일회성 사용, 다른 클라이언트의 confirm 차단, 재발급·취소·연결 종료, 만료·시도 제한을 실제 WebSocket으로 검사한다. 설치 소비자도 같은 페어링 검사를 실행한다. 기존 파일·로컬/원격 전송 API와 `helloWorld()`는 유지한다.

## 파일 계약

| op | 요청 필드 | 응답 |
| --- | --- | --- |
| `list` | `path`, 선택적 문자열 `cursor` | 최대 256개 `entries`, `nextCursor` |
| `stat` | `path` | 문자열 `size`, 불투명 `version` |
| `read` | `path`, 문자열 `offset`, 선택적 `version` | 최대 256 KiB의 base64 `data`, `size`, `offset`, `version`, `eof` |
| `write` | `path`, base64 `data` | 새 파일만 원자적으로 생성, 최대 256 KiB, 기존 파일은 보존 |
| `mkdir` | `path` | 기존 부모 아래 새 디렉터리 |

정수 크기·오프셋은 JSON 부동소수점 손실을 피하기 위해 문자열로 전달한다. 다운로드는 먼저 `stat`, 이어 같은 `version`으로 모든 청크를 받아야 한다. 변경된 파일은 `file_changed`로 실패한다. 디렉터리 페이지 커서는 해당 디렉터리가 변경되면 새로 시작해야 한다. 삭제·덮어쓰기·대용량 업로드·동기화 충돌 해결·OS 마운트는 이 API에 포함되지 않는다.

Unix 파일 제공은 `openat`, `O_NOFOLLOW`, 디렉터리 FD 및 inode/device 검사를 사용한다. 심볼릭 링크, 부모 경로 이동, 리다이렉션, 교체된 루트를 거부한다. 새 파일은 같은 디렉터리의 임시 파일을 완성한 뒤 `linkat`으로 게시하므로 기존 파일을 덮어쓰지 않는다. root guard로 Society UUID 등 애플리케이션 계약도 검사한다. Windows에서는 기본 `FileShare`가 안전한 네이티브 핸들 구현 전까지 요청을 거부한다. 전송·중계 계층과 사용자 정의 핸들러는 Qt 기반으로 분리되어 있다.

메시지 1 MiB, 송신 대기열 4 MiB, 연결 128개, 기기당 동시 요청 32개, 중계 요청 총 1024개, 로컬 연결 64개로 제한한다. 중계 운영자는 파일 트래픽을 볼 수 있다. 중계까지 각 연결은 TLS지만 종단 간 암호화 저장소는 아니다. 중계에 계정 쿠키나 파일 데이터를 로깅하지 않는다.

## 중계 실행

```sh
build/ii-server-relay --address 0.0.0.0 --port 9443 \
  --certificate /secure/fullchain.pem --key /secure/private-key.pem \
  --session-url https://iisacc.com/Account/Session
```

기존 HTTPS reverse proxy를 사용할 때는 중계를 `127.0.0.1:9443`에 바인딩하고 외부에 WSS만 제공한다. proxy는 WebSocket upgrade를 지원하고 연결 수명은 60초보다 길어야 한다. 소비 앱은 공용 중계 URL을 설정해야 한다. 이 저장소의 구현·loopback 검증은 공용 서버 배포나 다른 물리 네트워크에서의 실행 증거가 아니다. 새로운 유료 인프라를 생성하지 않는다.

## 빌드·설치

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

`INSTALL_PREFIX=... ./install.sh`도 같은 검증을 실행한다. 설치 타깃은 `iiServerHost::iiServerHost`, 공개 헤더는 `iiServerHost.h`, `ServerHost.h`, `PairingLink.h`, `iiServerHostExport.h`이다. 헤더 간 순환 참조를 두지 않는다. iOS에는 `BUILD_SHARED_LIBS=OFF`, `BUILD_TESTING=OFF`를 사용한다. 기존 `helloWorld()` ABI는 유지한다. 모든 빌드 산출물은 `build/` 아래에 둔다.

## 의존성 검토

기존 Qt 6.8.3 Core에 Network와 WebSockets만 추가했다. 유지보수되는 Qt 네트워크·TLS·WebSocket 구현을 사용하며 자체 암호화나 HTTP 파서를 만들지 않는다. Qt HTTP Server, WebRTC, 별도 유료 터널·VPN 의존성을 추가하지 않는다. Qt WebSockets의 공식 사용 조건은 상용 또는 LGPLv3/GPLv2이다. 실제 배포는 선택한 Qt 라이선스와 TLS 백엔드의 고지를 유지한다. OpenSSL 명령행은 테스트 인증서 생성용이며 SDK 직접 링크 의존성은 아니다.

macOS Secure Transport가 로그인 키체인에 서버 키를 넣지 않도록, 리스너는 미설정된 `QT_SSL_USE_TEMPORARY_KEYCHAIN`을 `1`로 초기화한다. 애플리케이션이 명시적으로 설정한 값은 보존한다.

근거: [Qt WebSockets](https://doc.qt.io/qt-6.8/qtwebsockets-index.html), [QWebSocket 메시지 제한](https://doc.qt.io/qt-6.8/qwebsocket.html), [QSslSocket 키체인·신뢰 설정](https://doc.qt.io/qt-6.8/qsslsocket.html).

## 라이선스

SPDX-License-Identifier: AGPL-3.0-only. 자체 코드는 [LICENSE](LICENSE)를 따른다. Qt 등 외부 라이브러리의 별도 라이선스를 대체하지 않는다.

`ii-server-host`는 소비 앱 없이도 같은 SDK를 사용하는 진단용 실행 파일이다. `--root`, `--relay`, `--id`, `--credential-file`로 호스팅하고, `--peer ID --path relative/path --read`로 한 청크를 읽거나 `--read` 없이 목록을 읽는다. 공개 LAN 리스너에는 `--certificate`와 `--key`를 함께 전달한다. `--no-local`로 원격 경로를 강제할 수 있다. 자격증명 파일은 소유자만 읽을 수 있게 두고 버전 관리에 포함하지 않는다. `iiServerHost.process_hosting`은 HTTP 인증 검증 서버와 중계·호스트·클라이언트의 별도 프로세스를 통해 양쪽 경로에서 실제 파일 바이트를 확인한다.

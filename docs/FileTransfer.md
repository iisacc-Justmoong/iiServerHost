# 표준 파일 전송과 서버 호스팅

iiServerHost 0.5.0은 기존 계정 기반 WebSocket 전송에 `FileTransfer`, `StorageBridge`, `FileProtocolServer`를 추가한다. 파일의 확장자나 내용에 따른 제한은 없다. 문서, 이미지, 동영상, 모델, 압축 파일과 임의 바이너리를 같은 경로로 전송한다. LAN 주소, DNS 이름과 인터넷 서버 주소를 사용할 수 있으며, 실제 도달 가능성은 서버·방화벽·네트워크 설정에 따른다.

## 지원 범위

| 방식 | 클라이언트 | 자체 서버 | 구현 |
| --- | --- | --- | --- |
| HTTP / HTTPS | 다운로드, PUT 업로드 | 파일 제공 | libcurl / rclone |
| WebDAV / WebDAVS | 다운로드, PUT 업로드, PROPFIND 목록 | 읽기·쓰기, 표준 WebDAV 작업 | libcurl / rclone |
| FTP | 다운로드, 업로드, 목록 | 읽기·쓰기 | libcurl / rclone |
| FTPS: 연결 시작부터 TLS | 다운로드, 업로드, 목록 | 읽기·쓰기, 제어·데이터 TLS 필수 | libcurl / pyftpdlib |
| FTPES: AUTH TLS | 다운로드, 업로드, 목록 | 읽기·쓰기, 제어·데이터 TLS 필수 | libcurl / pyftpdlib |
| SFTP | 다운로드, 업로드, 목록 | 읽기·쓰기, SSH 호스트 키 | libcurl / rclone |
| SCP | 다운로드, 업로드 | 이 SDK의 서버에는 포함되지 않는다 | libcurl |
| SMB / CIFS | 구성된 NAS의 읽기·쓰기·목록 | Samba/OS 서버를 별도로 운영한다 | rclone |
| S3 호환 저장소 | 객체·디렉터리 복사, 목록, 검증; HTTP SigV4 전송 | S3 API, SigV4 인증, 선택적 TLS | rclone / libcurl |
| NFSv3 | OS에 마운트된 경로를 로컬 파일로 사용 | 읽기·쓰기 | rclone |
| restic REST | restic 표준 클라이언트가 접속 | 저장소 제공, 선택적 TLS | rclone |
| TFTP | 다운로드, 업로드 | 포함되지 않는다 | libcurl |
| Gopher / Gophers | 바이너리 다운로드 | 포함되지 않는다 | libcurl |
| 로컬 파일 / 마운트된 공유 폴더 | 다운로드, 업로드, 디렉터리 복사 | 위 서버들의 파일 루트로 사용 | Qt / libcurl / rclone |
| 기존 Society 계정 기반 WS/WSS | 기존 `Peer`, `LanPeer` API | 기존 `RelayServer`, `Peer` API | Qt WebSockets |

직접 전송의 지원 여부는 `FileTransfer::protocols()` 또는 `ii-file-transfer --capabilities`가 반환하는 **실제 연결된 libcurl**의 기능으로 결정한다. 이 개발 환경의 libcurl 8.22.0에서는 13개 URL 스킴을 제공한다. SSH가 빠진 다른 libcurl 빌드에서는 SFTP/SCP가 목록에 나타나지 않는다. SMB/SMBS를 포함한 libcurl에서는 해당 스킴도 제공하지만, 현대 NAS 연결에는 rclone의 SMB 백엔드를 사용한다. libcurl의 SMBS를 SMB3의 자체 암호화와 동일한 의미로 취급하지 않는다.

`StorageOperation::Providers`는 선택한 rclone의 `config providers` JSON을 그대로 제공한다. 검증한 rclone 1.75.1에는 69개 저장소·가상 백엔드가 있다. SMB, SFTP, FTP, WebDAV, S3, Azure Blob/Files, Google Cloud Storage/Drive, OneDrive, Dropbox, B2, Swift, HDFS 등을 같은 파일 작업 API로 접근할 수 있다. 숫자는 프로토콜 69종을 뜻하지 않으며, 원격 공급자의 인증·API 제약과 읽기 전용 여부는 해당 백엔드에 따른다. [백엔드별 기능](https://rclone.org/overview/)을 확인할 수 있다.

모든 전송 규약과 모든 서비스의 모든 기능을 구현했다고 가정하지 않는다. 메일 전송, RTSP 제어, BitTorrent, IPFS 게이트웨이, QUIC 자체를 별도 파일 전송 API로 제공하지 않는다. `httpVersions()`로 사용 가능한 HTTP 버전을 조회하고 `httpVersion="1.0"`, `"1.1"`, `"2"`, `"3"`을 선택한다. 빈 값은 자동 선택이다. HTTP/2·HTTP/3는 libcurl의 이전 버전 fallback을 허용하며, 결과의 `httpVersion`이 실제 협상된 버전이다. HTTP/3 지원이 빠진 libcurl에 HTTP/3를 요청하면 전송 전에 거부한다. 실제 HTTP/3 연결은 QUIC를 제공하는 서버가 필요하다. [libcurl HTTP 버전 계약](https://curl.se/libcurl/c/CURLOPT_HTTP_VERSION.html)을 따른다.

## 직접 전송 API

```cpp
#include <iiServerHost.h>

iiServerHost::FileTransfer transfer;
iiServerHost::TransferRequest request;
request.url = QUrl("sftp://nas.example/Files/model.safetensors");
request.localPath = "/absolute/local/model.safetensors";
request.username = serviceUser;
request.password = servicePassword;
request.sshKnownHosts = "/absolute/private/known_hosts";
request.expectedSha256 = expectedHexDigest;
QObject::connect(&transfer, &iiServerHost::FileTransfer::finished,
    [](const QString &id, const iiServerHost::TransferResult &result) {
        // result.ok, error, bytes, sha256, responseCode
    });
const auto id = transfer.start(request);
// transfer.cancel(id);
```

호출과 신호 수신은 객체 소유 Qt 스레드에서 수행한다. 최대 4개 작업을 별도 작업 스레드에서 수행하므로 UI 이벤트 루프에서 네트워크를 기다리지 않는다. `start()`는 항상 ID를 반환하고 완료 신호는 비동기로 한 번 발생한다. `progress`는 바이트 진행 상황이다. 기본 연결 제한은 10초, 전체 제한은 5분이며 요청마다 조정할 수 있다. `maximumBytes=0`은 용량 제한 없음이다. 데이터는 메모리 전체 적재 없이 파일로 흐른다.

다운로드와 목록 출력은 `QSaveFile`을 사용한다. 전송 성공과 선택적 SHA-256 일치를 확인한 후에만 목적지를 교체한다. 실패·취소·해시 불일치·용량 초과 시 기존 파일을 보존한다. 업로드의 SHA-256은 전송한 로컬 바이트의 해시이며 서버 저장소 전체의 무결성 증명은 아니다. 업로드에 기대 해시를 주면 송신 전에도 원본을 검사한다. 원격 업로드의 원자성·중단 후 남은 데이터·동시 수정은 원격 프로토콜과 서버의 계약에 따른다. 앱은 전송 중 업로드 원본을 수정하지 않아야 한다.

`List`의 `localPath`는 목록을 저장할 파일이다. FTP/SFTP는 서버의 이름 목록, WebDAV는 Depth 1 PROPFIND XML을 기록한다. 구조화된 공통 목록이 필요하면 `StorageBridge`의 `List`를 사용한다. 디렉터리 복사는 `CopyDirectory`를 사용한다.

URL에 사용자 이름·비밀번호를 넣을 수 없다. HTTPS/TLS는 인증서와 호스트를 검증하며 검증 해제 옵션이 없다. SFTP/SCP는 명시한 `sshKnownHosts`가 필수이며 알려지지 않거나 변경된 호스트 키를 거부한다. FTPES/FTPS는 데이터 연결까지 TLS를 요구하며 평문으로 강등하지 않는다. 평문 네트워크 프로토콜은 숫자형 loopback에서 허용하고, 다른 주소는 `allowCleartext=true`를 명시해야 한다. 외부 URL로의 리디렉션은 자동 추적하지 않으며 `redirect_requires_new_request`를 반환한다. 시스템 proxy/netrc 설정과 자격 증명을 자동으로 가져오지 않는다.

HTTP에는 사용자/비밀번호, Bearer 토큰, 클라이언트 인증서, 별도 CA 파일을 지원한다. HTTP/3 우선 시도에는 HTTPS URL이 필요하다. S3 직접 전송은 HTTP(S) URL과 `awsSigV4="aws:amz:REGION:s3"`, `username=accessKey`, `password=secretKey`를 사용한다. 오류는 자격 증명, URL 쿼리, 서버 응답 본문을 포함하지 않는 코드로 반환한다.

## NAS·클라우드 공통 작업

```cpp
iiServerHost::StorageBridgeOptions backend;
backend.executable = "/absolute/tools/rclone";
backend.configFile = "/absolute/private/rclone.conf";
backend.runtimeDirectory = "/absolute/private/runtime";
iiServerHost::StorageBridge storage(backend);
iiServerHost::StorageRequest request;
request.operation = iiServerHost::StorageOperation::CopyFile;
request.source = "/absolute/local/model.bin";
request.destination = "NAS:Files/model.bin";
storage.start(request);
```

`source`와 `destination`은 절대 로컬 경로 또는 명시한 구성 파일에 있는 `remote:path`이다. rclone의 인라인 `:backend,option=value:` 문법과 상대 로컬 경로는 받지 않는다. `Providers`, `List`, `Stat`, `CopyFile`, `CopyDirectory`, `MakeDirectory`, `RemoveFile`, `Check`를 제공한다. `Check`는 양쪽 디렉터리 내용을 다운로드하여 비교하므로 공통 서버 해시가 없는 조합도 검사할 수 있다.

`overwrite=false`는 이미 있는 목적지 파일을 건너뛴다. 기존 파일을 성공적으로 새 내용으로 바꿨다는 의미가 아니므로, 정확한 내용 일치는 `Check`로 확인한다. `overwrite=true`는 기존 파일 교체를 허용한다. 이는 원격의 경쟁 쓰기에 대한 원자적 조건부 생성 보장이 아니다. 디렉터리 동기화에 수반되는 암묵적인 삭제는 수행하지 않으며, 삭제는 `RemoveFile`로 특정 파일을 명시한다.

셸을 실행하지 않고 고정된 명령과 분리된 인자를 사용한다. 현재 계정의 기본 rclone 구성, 환경의 클라우드 자격 증명과 `RCLONE_*` 옵션을 물려받지 않는다. 암호화된 구성에는 `configPassword`를 지정한다. 구성은 신뢰하는 운영자가 작성해야 한다. 각 백엔드의 호스트 키·TLS·OAuth·권한 설정은 이 명시적 구성에 따른다. Unix 구성 파일은 0600, 런타임 디렉터리는 0700으로 준비하며, 계정마다 별도 런타임 디렉터리를 사용한다. 구조화된 응답은 16 MiB까지이며 대형 목록은 더 작은 하위 디렉터리로 나누어 요청한다. 파일 데이터 용량과는 별개이다.

## 표준 서버 실행

`FileProtocolServer`는 한 개의 관리되는 자식 서버 프로세스를 소유한다. 주소와 포트를 지정하고 기본값은 loopback·읽기 전용이다. 인증 가능한 프로토콜은 사용자/비밀번호 또는 SFTP 공개 키 인증이 필수이다. HTTPS, WebDAVS, S3 TLS, restic TLS에는 인증서와 개인 키를 지정한다. SFTP에는 재시작 후에도 같은 신원을 사용하는 SSH 개인 키를 지정한다. 개인 키·구성·캐시를 제공할 파일 루트 안에 둘 수 없다.

`start()` 성공은 시작 요청 접수이며 `started()`는 자식 프로세스가 실행되었다는 뜻이다. 포트 준비·프로토콜 인증 성공은 클라이언트로 확인해야 한다. 프로세스 오류·종료는 `failed()`와 `stopped()`로 전달한다. `stop()`은 종료 신호 후 2초 안에 종료되지 않으면 강제 종료한다. 객체 소멸도 자식 프로세스를 정리한다. 운영 중 중단은 진행 중인 업로드에 영향을 줄 수 있다.

FTPS/FTPES는 `backend.ftpPythonExecutable`과 `backend.ftpServerScript`를 지정하며 **로컬 파일 루트만** 제공한다. 스크립트는 설치의 `share/iiServerHost/ftp_server.py`에 있다. Python 환경에 `tools/ftp-requirements.txt`의 모듈을 설치한다. rclone 1.75.1의 FTP TLS 서버는 데이터 보호 명령을 제공하지 않아 이 용도로 사용하지 않는다. 별도 pyftpdlib 서버는 TLS 1.2 이상과 제어·데이터 암호화를 모두 강제한다. FTP 계열은 제어 포트 외에 `passivePorts` 범위를 방화벽에 구성해야 하며 NAT에서는 `publicAddress`를 지정할 수 있다.

NFSv3에는 계정 인증이 없다. loopback 또는 운영자가 별도로 통제하는 네트워크·터널에서 사용한다. 쓰기를 허용하면 VFS 디스크 캐시와 즉시 write-back을 구성한다. S3 서버와 NFS 서버는 [rclone에서 실험 기능으로 분류](https://rclone.org/commands/rclone_serve/)되는 범위를 따른다. restic 서버는 `readOnly=true`를 제공하지 않으므로 쓰기 허용을 명시해야 한다. NFS와 restic은 `FileTransfer` URL 스킴이 아니며 각 표준 클라이언트가 접속한다.

기존 Society 계정의 검증은 `SessionAuthenticator`/소비 앱에 남는다. 서버의 `username`을 계정 식별자로 사용하는 것만으로 계정 인증이 성립하지 않는다. 소비 앱은 검증된 계정에 별도의 서비스 자격 증명과 해당 파일 루트를 연결해야 한다. 이 API 추가가 Society UI의 모든 새 프로토콜 설정이나 공용 서버 배포를 의미하지 않는다.

## 실행 도구

`ii-file-transfer --request request.json`과 `ii-storage --config storage.json`은 private JSON을 읽고 결과 JSON을 표준 출력으로 반환한다. 성공은 종료 코드 0이며 실패는 1 또는 구성 오류 2이다. 요청 JSON은 64 KiB까지이다. SIGINT/SIGTERM은 작업을 취소한다.

```json
{
  "operation": "download",
  "url": "https://files.example/model.bin",
  "localPath": "/absolute/model.bin",
  "expectedSha256": "EXPECTED_64_HEX_CHARACTERS",
  "maximumBytes": "10737418240"
}
```

`maximumBytes`는 정밀도를 보존하도록 JSON 문자열로 쓴다. 직접 전송의 요청 필드 이름은 `TransferRequest`와 같다. 서버 예시는 다음과 같으며, 설정 파일에 0600 권한을 부여한 후 `ii-storage --serve --config server.json`으로 실행한다.

```json
{
  "backend": {
    "executable": "/absolute/tools/rclone",
    "runtimeDirectory": "/absolute/private/runtime"
  },
  "server": {
    "protocol": "webdavs",
    "root": "/absolute/Files",
    "address": "0.0.0.0",
    "port": 9443,
    "username": "account-service-user",
    "password": "REPLACE_WITH_SERVICE_SECRET",
    "certificate": "/absolute/private/fullchain.pem",
    "privateKey": "/absolute/private/key.pem",
    "readOnly": false
  }
}
```

저장소 작업은 같은 `backend`와 `request` 객체를 사용한다. `request.operation`은 `providers`, `list`, `stat`, `copy-file`, `copy-directory`, `mkdir`, `remove-file`, `check`이며 `source`, `destination`, `overwrite`, `timeoutMs`를 지정한다. 서버 프로토콜 이름은 `http`, `https`, `webdav`, `webdavs`, `ftp`, `ftps`, `ftpes`, `sftp`, `s3`, `s3s`, `restic`, `restics`, `nfs`이다.

## 의존성·빌드·검증

기존 Qt 6.8.3을 재사용한다. libcurl은 C API로 연결하며 SSH/TLS/HTTP 기능은 그 빌드의 의존성에 따른다. rclone과 선택적 Python FTP 서버는 필요할 때 별도 프로세스로 실행한다. 대형 클라우드 SDK들을 C++ SDK에 직접 링크하지 않는다. libcurl은 curl 라이선스, rclone과 pyftpdlib는 MIT, pyOpenSSL은 Apache 2.0이다. 고지는 `ThirdParty/`와 설치 `share/iiServerHost/licenses/`에 제공한다. 사용한 버전과 원문은 [libcurl](https://curl.se/docs/copyright.html), [rclone](https://rclone.org/licence/), [pyftpdlib](https://github.com/giampaolo/pyftpdlib), [pyOpenSSL](https://www.pyopenssl.org/)에서 확인할 수 있다. 테스트 전용 AsyncSSH·Impacket은 SDK 실행 의존성이 아니다.

데스크톱은 `IISERVERHOST_WITH_CURL=ON`, `IISERVERHOST_WITH_STORAGE_PROCESS=ON`이 기본이다. libcurl 7.85 이상과 개발 헤더가 필요하며 `CURL_ROOT`로 선택한다. 모바일은 두 옵션이 기본 OFF이다. 모바일용 libcurl을 별도로 준비한 경우 직접 전송을 활성화할 수 있다. 프로세스 백엔드를 비활성화하면 QProcess 헤더를 포함하지 않는 구현으로 빌드하고 `external_backend_disabled`를 반환한다. 기존 WS/WSS 기능은 유지된다.

```sh
python3 tools/setup-protocol-tests.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/Volumes/Storage/Qt/6.8.3/macos \
  -DCURL_ROOT=/opt/homebrew/opt/curl \
  -DPython3_EXECUTABLE="$PWD/build/protocol-deps/venv/bin/python" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/protocol-stage"
cmake --build build --parallel 4
# setup-protocol-tests.py가 출력하는 rclone 경로를 사용한다.
IISERVERHOST_TEST_RCLONE="$PWD/build/protocol-deps/rclone-v1.75.1-osx-arm64/rclone" \
  ctest --test-dir build --output-on-failure
cmake --install build
cmake -S tests/consumer -B build/protocol-consumer/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/protocol-stage;/Volumes/Storage/Qt/6.8.3/macos" \
  -DPython3_EXECUTABLE="$PWD/build/protocol-deps/venv/bin/python"
cmake --build build/protocol-consumer/build --parallel 4
IISERVERHOST_TEST_RCLONE="$PWD/build/protocol-deps/rclone-v1.75.1-osx-arm64/rclone" \
  ctest --test-dir build/protocol-consumer/build --output-on-failure
```

준비 스크립트는 고정 버전 rclone을 공식 HTTPS 배포에서 받아 SHA-256을 확인하고 `build/`에만 테스트 의존성을 준비한다. 전역 패키지·실제 NAS 설정·클라우드 계정·유료 리소스를 변경하지 않는다. 프로토콜 테스트는 임시 루트와 loopback 서버를 사용한다. 공용 WAN, 실제 NAS 하드웨어, 클라우드 계정별 OAuth, 모바일 기기 실행은 별도의 검증이다.

SMB2 통합 테스트는 Impacket 0.13.1 서버로 인증·업로드·다운로드·목록·삭제를 검사한다. 이 서버의 실험 구현에서 관측한 LOGOFF 세션 ID, 없는 파일의 CREATE 상태, EOF READ 오류 구조는 `tests/smb_fixture.py`에 명시한 보정으로 표준 응답에 맞춘다. SDK의 SMB 클라이언트는 수정하지 않는다. 이 결과가 모든 NAS 제조사·SMB3 암호화 조합을 검증했다는 의미는 아니다. [SMB2 LOGOFF 응답](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/0936b9c0-51a3-4e7f-8fdf-de008f2675ed)과 [Samba의 CREATE 검사](https://github.com/samba-team/samba/blob/master/source4/torture/smb2/create.c)를 기준으로 삼는다.

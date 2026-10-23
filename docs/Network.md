# 네트워크 구현과 소비자 전환

이 문서는 기존 QObject 기반 `Compatibility` 컴포넌트의 전환 기록이다. Qt 없는 임베디드 HTTP API와 Nginx 하이브리드 배포는 [HttpServer.md](HttpServer.md)의 `iiWebServer::Http` 계약을 따른다. 아래 2026-10-23 실행 기록은 이름 변경·기존 네트워크 전환 당시의 결과이며, 후속 HTTP 구현 검증과 구분한다.

공개 패키지·타깃·네임스페이스는 `iiWebServer`, `iiWebServer::iiWebServer`, `iiWebServer`이다. 통합 헤더는 `iiWebServer.h`, 서버 API 헤더는 `WebServer.h`이다. 명령행 호스트는 `ii-web-server`이며 환경변수와 CMake 옵션은 `IIWEBSERVER_*`를 사용한다. 소스는 C++23으로 빌드한다.

## 전송 계층

TCP 연결과 리스너, TLS 1.2/1.3, RFC 6455 WebSocket 프레이밍, 인증 서버에 보내는 HTTP 요청과 IP 주소·인터페이스 조회는 `iiNetwork` 0.5 이상을 사용한다. Qt Network와 Qt WebSockets를 빌드·설치 의존성으로 요구하지 않는다. GUI 소비자가 사용하는 기존 QObject 신호와 QString/QJsonObject 공개 계약은 Qt Core 경계에서 유지한다. `NetworkTransport`는 이 신호 경계와 iiSDKCore 이벤트 루프의 호스트 구동만 담당한다.

`Address`는 `ii::network::IpAddress`, `TlsConfiguration`은 `ii::network::TlsOptions`의 별칭이다. 주소는 `Address::parse()`의 성공 여부를 확인하여 구성한다. 루프백은 `Address::loopback()`, IPv4 전체 바인딩은 `Address::any()`이다. PEM 파일을 쓰는 서버는 `tls.policy.identity.certificate_chain_file`과 `private_key_file`을 함께 설정한다. 릴레이 신뢰 CA는 `tls.policy.ca_file`로 지정한다. 인증서·키 불일치와 유효하지 않은 TLS 정책은 연결을 거부한다.

LAN 호스트의 임시 인증서와 개인 키는 `ii::network::TlsIdentity`의 메모리 PEM 필드로 전달한다. 개인 키를 파일이나 시스템 키체인에 설치하지 않는다. 클라이언트는 인증된 디렉터리 또는 QR에 담긴 정확한 인증서 SHA-256을 `TlsPolicy::trusted_certificate_sha256`에 지정한다. 이 명시적 신뢰는 해당 인증서의 CA·호스트명 판단만 대체하며, 인증서 유효기간·용도·TLS 보안 제한과 정확한 지문 일치를 계속 검증한다. 공개 릴레이는 기본 CA·호스트명 검증을 사용한다.

## 수명과 제한

네트워크 콜백과 Qt 신호는 생성 스레드에서 전달한다. DNS 조회는 iiSDKCore의 제한된 공용 작업자에서 수행하고 결과를 소유 이벤트 루프로 전달한다. 연결 해제·소멸 이후 조회 결과는 적용하지 않는다. 핸드셰이크 제한은 5초, 메시지는 1 MiB, WebSocket 출력은 4 MiB이며 네이티브 스트림에도 별도 상한을 적용한다. 같은 스레드에서 요청·중지·소멸이 재진입할 수 있으므로 콜백은 소유 상태와 수명을 다시 확인한다. 다시 연결하는 중 기존 연결 해제 신호에서 소켓을 삭제해도 새 연결을 생성하지 않는다.

릴레이 계정 인증, 단회 티켓, QR 만료·확인·취소, 바이너리 전송 협상과 파일 접근 제한은 기존 계약을 유지한다. 평문 리스너는 명시적 루프백 주소에만 허용한다. 실제 파일 전송의 libcurl 및 외부 rclone 프로토콜 지원은 각각의 기존 옵션을 따른다.

## 검증

기존 LAN·호스팅·페어링·파일 전송 테스트와 독립 설치 소비자를 실행한다. 테스트의 HTTP 오류 주입 서버도 iiNetwork TCP를 사용한다. Qt Network/WebSockets의 패키지 검색을 비활성화한 설치 소비자를 구성하고, 생성된 라이브러리의 실제 링크 의존성도 별도로 확인한다. 플랫폼별 실행 결과는 해당 플랫폼에서 실행한 경우에만 검증 결과로 기록한다.

### 2026-10-23 실행 결과

macOS arm64, Qt Core/Test 6.8.3, OpenSSL 4.0.3에서 Release 빌드와 Workspace 내 설치 패키지를 검증하였다. `iiNetwork`는 소스 테스트 28/28과 독립 설치 소비자 16/16이 통과하였다. 만료·용도 오류·지문 오류·메모리 인증서/키 불일치를 포함한 실제 TLS 검증도 여기에 포함한다.

이 모듈의 전체 소스 테스트는 12개 중 10개가 통과하고 외부 rclone을 사용하는 `storage`, `protocol_processes`가 기동 제한 시간 초과로 실패하였다. 같은 rclone 바이너리의 독립 `version` 실행도 45초 내에 끝나지 않았다. 두 항목을 제외한 최종 소스 실행에서는 9/10이 통과하고 `hosting`이 60초 제한을 초과하였으며, 이어진 독립 재실행에서는 `hosting`이 38.62초에 통과하였다. 이 결과를 단일 전체 실행의 완전 통과로 합산하지 않는다. 설치 소비자도 전체 실행은 같은 rclone 두 항목을 제외한 7/9이며, 최종 핵심 선택 실행은 7/7이 통과하였다.

`iiSocietyHelper`와 세션·동기화 소비자 실행 파일을 새 패키지로 빌드하였다. `Sync.WatchPaths`와 `Sync.Namespace`는 통과하였다. 세션·컨트롤러 전체 회귀에는 시간 초과가 있으며, 최종 `Sync.SocketTransfer`는 네트워크 연결을 시작하기 전 저장소 준비가 5초 제한을 초과하여 실패하였다. 이를 전송 전체의 통과 증거로 사용하지 않는다. Society·Dreamscapes의 변경된 C++ 소스 10개는 새 헤더를 사용한 구문 검사를 통과하였고, 완성된 앱의 런타임 재검증은 수행하지 않았다.

설치된 공개 헤더는 iiWebServer 9개와 iiNetwork 20개가 소스와 일치한다. `otool -L`에서 생성·설치된 iiWebServer 라이브러리의 직접 의존성에는 Qt Network와 Qt WebSockets가 없다. Qt Core 신호 경계는 유지한다. 설치 검증 위치는 각각 `build/package`이며, 기존 사용자 전역 설치와 실행 중인 앱은 교체하지 않았다. Windows·Linux·Android·iOS의 네이티브 빌드·실행 결과는 이 기록에 포함하지 않는다.

소스·설치 소비자 로그는 저장소 루트 기준 `build/rename-tests.log`, `build/final-network-tests.log`, `build/final-hosting-isolated.log`, `build/consumer-tests.log`, `build/final-consumer-tests.log`, `build/downstream-final-build.log`, `build/downstream-final-network-tests.log`에 있다. Workspace 전체 변경 및 검증 목록은 Workspace 루트의 `build/iiwebserver-migration/result.json`에 기록한다.

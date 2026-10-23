# 임베디드 HTTP 서버와 Nginx 배포

`iiNetwork`는 비동기 소켓·TLS·HTTP 파싱·연결 수명을 담당한다. `iiWebServer::Http`는 그 위에서 애플리케이션 라우팅과 미들웨어를 제공한다. 공개 인터넷에서는 Nginx가 TLS 종료, 정적 파일, 요청 제한 및 프록시를 담당하고, 로컬 앱은 같은 HTTP 서버를 직접 구동한다. 성공 기준은 동일한 iiSDK 애플리케이션 기능을 별도 서버 기술 스택 없이 제공하는 것이다.

네이티브 소비자는 `<iiWebServer/HttpServer.h>`와 `iiWebServer::Http`를 사용한다. 기존 QObject 기반 LAN·릴레이·파일 전송은 `Compatibility` 계층이며 기본 빌드에서 기존 소비자의 동작을 유지한다. HTTP 핵심 타깃은 Qt와 iiFileProvider에 의존하지 않는다.

## 소비 API

```cmake
find_package(iiWebServer 0.6 CONFIG REQUIRED COMPONENTS Http)
target_link_libraries(my_application PRIVATE iiWebServer::Http)
```

```cpp
#include <iiWebServer/HttpServer.h>

int main() {
    ii::runtime::EventLoop loop;
    iiWebServer::HttpServer server(loop);
    auto configured = server.route("GET", "/items/{id}",
        [](const iiWebServer::HttpRequest&, const iiWebServer::RouteParameters& parameters) {
            return iiWebServer::HttpResponse{200, {{"Content-Type", "text/plain"}}, parameters.at("id")};
        });
    if (!configured || !server.listen({ii::network::IpAddress::loopback(), 8080})) return 1;
    loop.run(); // GUI 호스트는 기존 호스트 루프에서 loop.poll()을 구동한다.
}
```

라우트와 미들웨어는 리스너를 시작하기 전에 등록한다. 중지 후 연결이 모두 정리되면 다시 구성할 수 있다. 실행 중인 서버에 `listen()`을 다시 호출하여 기존 요청을 중단시키지 않는다. 미들웨어는 등록 순서대로 진입하고, `next(request)` 뒤의 처리는 역순으로 실행한다. 인증 미들웨어는 `next`를 호출하지 않고 응답하여 단락시킬 수 있다. 체인은 최대 64개이며 `next`는 현재 호출 안에서 동기적으로 사용한다. 라우트 매개변수는 `{id}` 형식이며 정적 경로가 매개변수 경로보다 우선한다. 잘못된 경로·메서드·중복 라우트는 iiNetwork 라우터의 검증을 따른다.

요청은 연결당 한 개이고 응답 후 닫는다. HTTP/1.0·HTTP/1.1의 origin-form과 Content-Length 프레이밍을 지원한다. 기본 한도는 연결 64개, 헤더 16 KiB, 본문·응답 각각 2 MiB, 요청 수신·응답 전송 각각 10초이다. 중복 framing, 모호한 authority, transfer coding, pipelining과 Expect는 거부한다. Nginx의 요청 버퍼링이 외부 chunked 업로드를 완전히 받은 뒤 백엔드로 전달한다. 백엔드 자체의 chunked 요청, keep-alive, 스트리밍 응답, HTTP/2·3은 현재 계약에 포함하지 않는다.

핸들러와 미들웨어는 이벤트 루프 소유 스레드에서 동기 실행된다. 예외는 내부 상세를 노출하지 않는 500 응답으로 정리한다. 긴 이미지 처리·모델 실행은 iiSDKCore 작업 큐에 넘기고 작업 ID를 반환하는 애플리케이션 프로토콜을 구성한다. HTTP 서버가 비즈니스 로직이나 Dreamscapes 엔진을 다시 구현하지 않는다.

## 종료와 신뢰 경계

`shutdown(grace)`는 신규 연결을 막고 이미 들어온 요청과 응답을 마무리한다. 호스트는 `draining()`이 거짓이 될 때까지 루프를 계속 구동한다. 최초 유예 기한은 반복 호출로 연장되지 않으며, 기한을 넘기면 남은 연결을 닫는다. `close()`와 소멸자는 즉시 닫는다. `listenLocal()`은 네이티브 로컬 소켓을 사용하므로 같은 호스트의 Nginx upstream을 Unix 소켓으로 구성할 수도 있다.

요청의 `peer_endpoint`, `local_endpoint`, `secure`는 실제 백엔드 연결의 정보이다. `X-Forwarded-*`나 `Forwarded` 헤더를 인증·권한·원격 IP 증거로 자동 승격하지 않는다. Nginx 예제는 외부에서 받은 전달 헤더를 실제 접속 정보로 교체하고 `Forwarded`를 제거한다. 외부 canonical origin과 계정 권한은 애플리케이션의 명시적 설정·인증 미들웨어가 소유한다.

## 로컬과 공개 배포

참조 실행 파일은 다음과 같이 실행한다.

```sh
./build/native-http/build/ii-http-server --address 127.0.0.1 --port 8080
```

`GET /health`와 `POST /api/echo`만 제공하는 참조 호스트이다. `--max-body-bytes`, `--request-timeout-ms`, `--grace-ms`로 배포별 한도를 설정하고 SIGINT/SIGTERM에서 정상 종료한다. 실제 애플리케이션은 동일한 라이브러리에 자신의 라우트와 공통 iiSDK 모듈을 연결한다. GUI·헤드리스 모드의 선택은 제품 실행 진입점의 책임이다.

공개 환경에서는 `deploy/nginx/iiwebserver.conf`를 기존 Nginx의 `http {}` 블록 안에 포함한다. 도메인, 인증서·키, 공개 정적 디렉터리를 실제 배포 값으로 변경하고, 백엔드는 기본 `127.0.0.1:8080` 또는 같은 호스트의 로컬 소켓으로 둔다. TLS 인증서의 발급·갱신·파일 권한과 정적 파일 배포는 Nginx 운영 단계에서 관리한다. 예제의 `http2 on` 구문은 SSL·HTTP/2 모듈이 있는 Nginx 1.25.1 이상을 요구한다. HTTP/3은 해당 Nginx 빌드와 UDP 진입점이 필요한 선택적 운영 기능이며 이 예제에서 활성화하지 않는다.

Nginx는 TLS 종료, HTTP/2, 정적 파일, IP별 요청 제한을 처리한다. 백엔드의 본문 한도와 프록시의 `client_max_body_size`는 함께 관리한다. 프록시 자동 재시도는 꺼서 쓰기 요청을 다른 upstream에 재실행하지 않는다. 기존 WebSocket 릴레이는 별도 리스너이며, 이를 공개할 때는 해당 location에 Upgrade/Connection 전달을 명시하는 별도 WebSocket 프록시 구성을 사용한다.

## 빌드와 검증

Qt 없는 전체 빌드는 다음과 같다. 모든 산출물은 `build/` 아래에 둔다.

```sh
cmake -S . -B build/native-http/build -DCMAKE_BUILD_TYPE=Release \
  -DIIWEBSERVER_BUILD_COMPATIBILITY=OFF -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="../iiNetwork/build/package;$HOME/.local/SDK/iiSDKCore;$HOME/.local/SDK/iiJson" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/native-http/build/package"
cmake --build build/native-http/build --parallel 3
ctest --test-dir build/native-http/build --output-on-failure
cmake --install build/native-http/build
```

기본 호환 빌드에서도 소비자가 `COMPONENTS Http`만 요청하면 Qt와 iiFileProvider를 검색하거나 호환 타깃을 import하지 않는다. 기존 `find_package(iiWebServer)`와 `iiWebServer::iiWebServer` 사용자는 기본 호환 계층을 유지한다.

iiNetwork를 먼저 같은 소스의 헤더·정적 라이브러리로 빌드·설치한다. 새 `ServerLimits`와 종료 API를 제공하는 `iiNetwork_HTTP_SERVER_LIFECYCLE` 표시가 없는 이전 패키지는 구성 단계에서 거부한다. `IINETWORK_PREFIX`는 설치 스크립트의 의존성 위치를 지정하며, `IIWEBSERVER_BUILD_COMPATIBILITY=OFF INSTALL_PREFIX=... ./install.sh`는 Qt 없는 빌드와 네이티브 설치 소비자를 검사한다.

`tests/http.cpp`는 실제 TCP로 라우팅·미들웨어·바이너리 본문·본문/헤더 한도·응답 프레이밍 경계·요청 기한·예외·콜백 중 소멸을 검증한다. `tests/http_cli.py`는 참조 프로세스의 실제 요청과 SIGTERM drain을 확인한다. `tests/http-consumer`는 설치된 Http 컴포넌트를 Qt·Provider 패키지 검색 없이 빌드한다. `IIWEBSERVER_TEST_NGINX`에 Nginx 실행 파일을 지정하면 `tests/nginx_http.py`가 TLS/HTTP2, 정적 파일, chunked 업로드 버퍼링, 본문·요청 속도 제한을 같은 참조 호스트에 연결하여 검증한다. 이 통합 검사는 HTTP/2 지원 curl과 OpenSSL CLI도 요구한다. Nginx는 SDK의 빌드·실행 의존성이 아니다.

### 2026-10-23 검증 결과

macOS arm64에서 다음 결과를 확인하였다. 실행 결과는 Windows·Linux·모바일의 네이티브 실행 증거를 포함하지 않는다.

| 검사 | 결과 |
| --- | --- |
| iiNetwork Release 전체 회귀 | 32/32 통과 |
| Qt·Provider 검색을 비활성화한 HTTP 빌드 | 실제 Nginx 1.30.6 통합을 포함하여 5/5 통과 |
| 호환 빌드의 HTTP·LAN·호스팅·페어링·전송 등 선택 회귀 | 11/11 통과 |
| 네이티브 패키지의 독립 Http 소비자 | 3/3 통과 |
| 호환 패키지의 독립 Http 소비자 | Qt·Provider 검색을 비활성화하여 3/3 통과 |
| 기존 기본 컴포넌트 소비자 | 별도 설치 패키지로 구성·빌드·실행 통과 |
| 양쪽 모듈의 정상 종료·HTTP 수명 ASan/UBSan | 6/6 통과 |

두 설치 패키지의 참조 실행 파일에서도 실제 HTTP 요청·본문 제한·SIGTERM 정상 종료가 통과하였다. 설치된 네이티브 공개 헤더는 소스와 일치하며, HTTP 라이브러리·참조 호스트·독립 소비자의 실제 링크 목록에는 Qt·iiFileProvider가 없다. iiNetwork의 설치 헤더와 HTTP 서버 오브젝트도 같은 빌드 산출물임을 확인하였다.

호환 선택 실행에는 `storage`, `provider_ftp`, `provider_ftps`, `fixture_cleanup`, `protocol_processes`를 포함하지 않았다. 초기 회귀 실행에는 외부 프로세스 제한 시간 초과가 있었고 CTest의 로그 쓰기 대기도 관측하였다. 실행 파일 기동 확인 뒤 기존 CTest 제한 시간으로 다시 수행한 최종 전체 iiNetwork 결과가 위 32/32이다. 내부 HTTP 요청·응답·종료 기한은 변경하지 않았다.

재현 로그와 범위는 Workspace의 `build/iiwebserver-http/result.json` 및 `build/iiwebserver-http/verify_packages.py`에 기록한다. 검증 설치본은 `iiNetwork/build/package`, `iiWebServer/build/native-http/build/package`, `iiWebServer/build/compatibility/build/package`이며 사용자 전역 설치는 교체하지 않았다.

## 근거

- [Nginx reverse proxy와 요청 버퍼링](https://nginx.org/en/docs/http/ngx_http_proxy_module.html)
- [Nginx 요청 속도 제한](https://nginx.org/en/docs/http/ngx_http_limit_req_module.html)
- [Nginx HTTP/2 모듈](https://nginx.org/en/docs/http/ngx_http_v2_module.html)
- [기존 WebSocket 릴레이의 프록시 경계](https://nginx.org/en/docs/http/websocket.html)

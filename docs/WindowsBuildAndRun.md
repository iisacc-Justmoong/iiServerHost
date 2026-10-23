# iiWebServer: Windows 빌드 및 실행

LAN TLS pairing, hosting 및 파일 전송을 제공하는 Qt 기반 서버 SDK이다.

검증 기록 갱신: `2026-10-20T02:45:37+09:00`. 변경 전 소스 기준 commit: `bc11b80f4003489210e9af658a0886591e4832ff`. 아래 결과는 이번 변경을 포함한 작업 트리의 검증이다.

## 현재 검증 상태

Release 빌드·실행 검사·설치를 완료하였다. 등록 검사 12개 중 12개 통과, 0개 실패, 0개 skip이다.

Windows rclone의 NFS 서버는 제공되지 않는다. 해당 요청을 `unsupported_protocol`로 거부하는 회귀 검사는 통과하였으며 NFS I/O 성공으로 계산하지 않는다. 지원되는 로컬 프로토콜 전송·hosting·SMB2 검사는 실제 실행하였다.

## 이 프로젝트의 의존성과 실행 범위

선행 workspace SDK는 다음과 같다. 각각의 Windows 지침에 따라 실행 검증과 설치를 완료한 뒤 진행한다.

- [iiFileProvider](../../../SDK/iiFileProvider/docs/WindowsBuildAndRun.md)

- iiFileProvider 및 Native, iiNetwork 0.5, Qt Core/Test, CURL 7.85 이상과 OpenSSL Crypto/SSL가 필요하다.
- 외부 storage process 기능을 실제로 사용하려면 별도로 해당 프로그램이 필요하다. fixture와 실제 서비스 연결을 구분한다.

프로젝트별 Windows 조건은 다음과 같다.

- 네트워크 테스트는 로컬 fixture 연결을 포함한다. 공개 서버 배포나 실제 클라우드 자격 증명 사용의 증거로 취급하지 않는다.

## 환경 준비

PowerShell에서 이 프로젝트 루트로 이동한 뒤 순서대로 실행한다. workspace 배치는 `Workspace/SDK/<이름>`을 전제로 한다.

검증 도구는 MSVC 19.51 x64, Windows SDK 10.0.28000, CMake 4.4.4, Ninja 1.13.2이다. 개발 shell은 빌드 재현을 위해 Python 3.14.8을 선택하며 시스템 기본 Python 3.15.0과 구분한다.

```powershell
$ErrorActionPreference = 'Stop'
$project = (Get-Location).Path
$workspace = (Resolve-Path -LiteralPath '../..').Path
. (Join-Path $workspace 'SDK/Windows/Enter-DevEnvironment.ps1')
Set-Location -LiteralPath $project
$python = (Join-Path $workspace 'Tools/Python314/tools/python.exe').Replace('\', '/')
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source.Replace('\', '/')
$toolchain = (Join-Path $workspace 'Tools/vcpkg/scripts/buildsystems/vcpkg.cmake').Replace('\', '/')
$msvcInclude = (Join-Path $workspace 'SDK/Windows/WindowsMsvcPrefix.cmake').Replace('\', '/')
$prefix = (Join-Path $env:USERPROFILE '.local/SDK/iiWebServer').Replace('\', '/')
```

`Enter-DevEnvironment.ps1`은 Qt MSVC kit, vcpkg, 설치된 SDK의 검색 경로와 UTF-8 설정을 준비한다. `CMAKE_PREFIX_PATH`의 디렉터리 존재만으로 의존성 검증을 대신하지 않는다. 선행 SDK의 실행 검사와 설치를 먼저 완료해야 한다.

## 구성과 빌드

```powershell
$configure = @(
    '-S', '.', '-B', 'build', '-G', 'Ninja'
    '-DCMAKE_BUILD_TYPE=Release'
    "-DCMAKE_C_COMPILER=$compiler"
    "-DCMAKE_CXX_COMPILER=$compiler"
    "-DCMAKE_INSTALL_PREFIX=$prefix"
    "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    '-DVCPKG_TARGET_TRIPLET=x64-windows'
    "-DCMAKE_PROJECT_INCLUDE=$msvcInclude"
    "-DPython3_EXECUTABLE=$python"
    "-DPython_EXECUTABLE=$python"
    '-DQt6_DIR=C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6'
    '-DBUILD_TESTING=ON'
    '-DIIWEBSERVER_WITH_CURL=ON'
    '-DIIWEBSERVER_WITH_STORAGE_PROCESS=ON'
)
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake 구성 실패' }
$cache = Get-Content -LiteralPath 'build/CMakeCache.txt' -Raw
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$') {
    throw 'Release 구성이 유실되었음. 같은 구성 인수에 --fresh를 추가하여 다시 구성할 것'
}
& $python (Join-Path $workspace 'SDK/Windows/Configure-WindowsNinja.py') (Join-Path $project 'build')
if ($LASTEXITCODE -ne 0) { throw 'Ninja MSVC 의존성 설정 실패' }
cmake --build build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw '빌드 실패' }
```

모든 생성 파일은 이 프로젝트의 `build/` 아래에 둔다. 컴파일러는 위에서 확인한 절대 경로를 사용한다. 기존 캐시가 다른 compiler 또는 generator를 사용한다면 같은 구성 인수에 `--fresh`를 추가하여 다시 구성한다. MSVC 키트와 MinGW 또는 Debug/Release 라이브러리를 혼합하지 않는다.

`WindowsMsvcPrefix.cmake`와 `Configure-WindowsNinja.py`는 MSVC include 출력 언어를 Ninja에 맞추고 UTF-8 실행 manifest를 적용한다. 빌드 병렬도는 4로 제한하며 실행 검사는 직렬로 수행한다.

## 실행과 확인

Windows rclone은 NFS 서버를 제공하지 않는다. 해당 요청은 실행을 시작했다고 알리기 전에 `unsupported_protocol`로 거부하며 이를 회귀 검사한다. Unix의 NFS MOUNT/LOOKUP/READ/WRITE 검사는 유지한다. Windows에서는 NFS 호스팅을 실행 성공으로 집계하지 않는다. 근거는 [고정 버전의 upstream NFS 빌드 조건](https://github.com/rclone/rclone/blob/v1.75.2/cmd/serve/nfs/nfs.go)이다.

Windows SMB2 검사는 .NET 10 SDK와 [SMBLibrary](https://github.com/TalAloni/SMBLibrary/tree/582635541d2ec0c84eac5f66cbebd41e2d35c7ae)의 고정 소스 commit을 사용한다. CMake가 원본 ZIP의 SHA-256을 확인한 뒤 `tests/SmbWindowsFixture`를 `build/smb-fixture`로 빌드한다. 원본 서버가 LOGOFF 응답의 서명 전에 세션 키를 제거하는 결함은 검사 전용 `prepare_source.py`에서 응답을 큐에 넣은 뒤 세션을 제거하도록 보정한다. 클라이언트 서명 검증은 유지한다. .NET의 동명 암호화 클래스와 충돌하지 않도록 원본 `Utilities.AesCcm`을 명시한다. LGPL-3.0 소스와 라이선스는 build 디렉터리에 보존하며 SDK 라이브러리·설치본에는 포함하지 않는다. fixture는 loopback의 임시 포트, 임시 디렉터리, 검사 전용 NTLM 인증만 사용한다. OS 사용자·공유·445 포트와 Defender 설정을 변경하지 않는다. Unix에서는 기존 Impacket fixture를 유지한다.

프로토콜 검사 Python에는 SCP fixture용 `asyncssh`도 필요하다. 네이티브 미리보기 파일을 생성한 직후 Windows가 짧게 공유 잠금을 유지하는 경우 검사 임시 폴더 정리는 최대 3초 재시도한다. 재시도 대상은 공유 위반 32/33만이며 지속 잠금과 다른 권한 오류는 실패로 남긴다. 별도 cleanup 회귀 검사가 이 조건을 확인한다.

임시 TLS/SSH 개인 키는 Windows에서 상속 DACL을 제거하고 현재 사용자에게만 접근을 허용한다. `chmod`의 owner 권한만으로는 NTFS ACL이 제한되지 않으므로 OpenSSH가 키를 거부할 수 있다. 시스템 인증서 저장소나 실제 사용자 키는 변경하지 않는다.

관리되는 자식의 home과 temp는 모두 전용 runtime 디렉터리로 지정한다. Windows Python의 `Path.home()`에 필요한 `USERPROFILE`을 제공하면서도 사용자 site package는 비활성화한다. implicit FTPS와 FTPES는 각각 실제 파일 전송 검사를 수행한다. 부모 강제 종료 후 포트가 닫히는 확인은 Windows Job Object의 비동기 정리 시간을 포함하여 5초로 제한한다.

실제 프로토콜 검사는 rclone 1.75.2와 `pyftpdlib`/`pyOpenSSL`을 사용한다. libcurl은 vcpkg의 `curl[openssl,ssh]:x64-windows` 구성으로 준비하여 사용자 지정 CA 파일과 SFTP known-host 검사를 지원한다. 기본 Schannel 구성에서 CRL이 없는 임시 CA fixture는 revocation 상태가 unknown으로 거부되므로 이 검증 환경은 OpenSSL backend를 사용한다. 인증서·호스트 이름 검증 옵션은 활성 상태로 유지한다.

```powershell
$env:IIWEBSERVER_TEST_RCLONE = Join-Path $workspace 'Tools/Rclone/rclone-v1.75.2-windows-amd64/rclone.exe'
$env:IIWEBSERVER_TEST_FTP_PYTHON = Join-Path $workspace 'Tools/PythonServerHost/Scripts/python.exe'
$env:PYTHONPATH = (Join-Path $workspace 'Tools/python-serverhost') + ';' + $env:PYTHONPATH
$env:CURL_SSL_BACKEND = 'openssl'
```

`Tools/PythonServerHost`는 build Python으로 생성한 전용 venv이며 pyOpenSSL과 pyftpdlib를 설치한다. 관리되는 FTP 자식은 외부 Python 경로 환경변수를 전달받지 않아도 이 venv의 패키지와 script 옆의 native adapter를 찾을 수 있어야 한다.

초기 설치는 아래 고정 requirements를 사용한다. Windows에서는 Impacket을 설치하지 않으며 SMB2는 위의 .NET fixture를 사용한다. 이번 SSH fixture의 실행 버전은 AsyncSSH 2.24.1이다.

```powershell
$ftpPython = Join-Path $workspace 'Tools/PythonServerHost/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $ftpPython)) {
    & $python -m venv (Join-Path $workspace 'Tools/PythonServerHost')
    if ($LASTEXITCODE -ne 0) { throw 'FTP Python 환경 생성 실패' }
}
& $ftpPython -m pip install -r tools/ftp-requirements.txt
if ($LASTEXITCODE -ne 0) { throw 'FTP 의존성 설치 실패' }
& $python -m pip install --target (Join-Path $workspace 'Tools/python-serverhost') -r tests/protocol-requirements.txt
if ($LASTEXITCODE -ne 0) { throw '프로토콜 검사 의존성 설치 실패' }
```

Windows의 외부 저장소 backend는 rclone에 빈 설정을 전달할 때 `NUL`을 사용한다. Qt의 Win32 장치 경로는 Go의 설정 경로 정규화에서 거부된다. 설정 파일과 런타임 디렉터리는 Windows DACL을 조회하여 Everyone·Authenticated Users·Users의 공개 읽기/쓰기 권한을 거부한다. 검사에서는 실제 Everyone 읽기 ACE를 추가한 설정 파일이 거절되는지 확인한다.

FTP Python adapter의 native DLL 옆에는 SQLite DLL도 배치한다. 현재 Python의 `ctypes`는 종속 DLL을 일반 `PATH`만으로 검색하지 않으므로 `iiWebServerProviderRuntime`이 두 파일을 같은 `build/lib`에 준비한다. TLS FTP 검사는 개발자 설치 경로 관련 환경변수를 제거한 상태에서 파일 생성·추가·이름 변경과 Unicode 전송을 실행한다.

외부 저장소 프로세스는 Windows Job Object에 연결하며 마지막 job handle이 닫힐 때 종료한다. 부모 host가 강제로 종료되어도 rclone/FTP 자식과 수신 포트가 남지 않아야 하며, 실제 프로토콜 검사가 이를 확인한다.

LAN, TLS pairing, hosting, file transfer 및 provider 파일 노출 경계를 검증한다.

```powershell
$dllDirectories = @(Get-ChildItem -LiteralPath build -Recurse -Filter '*.dll' -File |
    Select-Object -ExpandProperty DirectoryName | Sort-Object -Unique)
$env:PATH = (@($dllDirectories) + @($env:PATH)) -join ';'
$env:QSG_RENDER_LOOP = 'basic'
ctest --test-dir build -C Release --output-on-failure --parallel 1 --no-tests=error
if ($LASTEXITCODE -ne 0) { throw '전체 실행 검사 실패' }
```

현재 빌드에 실제 등록된 실행 경로의 예는 `iiWebServer.lan`, `iiWebServer.hosting`, `iiWebServer.pairing`이다. 전체 목록은 `ctest --test-dir build -N`으로 확인한다.

## 설치와 배포 범위

전체 테스트가 통과한 구성만 설치한다. skip된 선택 기능은 지원·실행 범위를 별도 기록한다.

```powershell
cmake --install build --config Release --prefix "$prefix"
if ($LASTEXITCODE -ne 0) { throw '설치 실패' }
```

설치 파일은 `build/install_manifest.txt`와 실제 prefix의 파일을 대조한다. 소비자 실행 시 prefix의 `bin`과 필요한 vcpkg DLL을 검색할 수 있어야 한다.

## 검증 근거

- 빌드 정의: [CMakeLists.txt](../CMakeLists.txt)와 프로젝트 테스트 등록이다.
- 이번 실행 결과: [Windows SDK 전체 검증 기록](https://github.com/iisacc-Justmoong/SDK/blob/main/Windows/Verification-20261020.md)이다.
- 프로젝트별 실행 로그와 JUnit은 같은 검증 디렉터리에 보관한다. 로그의 종료 코드, 실패·skip 목록과 실제 실행 범위를 함께 확인한다.

전체 SDK의 실행·설치와 커밋 조건은 [공통 Windows 검증 도구](https://github.com/iisacc-Justmoong/SDK/blob/main/Windows/README.md)에서 재현한다. 원본 JSON·JUnit·상세 로그는 로컬 `Workspace/build/maintenance/20261019-221920-sdk-recovery/final/`에 보관하며 Git에는 포함하지 않는다. 이 프로젝트의 성공은 제품·다른 플랫폼·별도 계정 및 물리 장치의 실연동을 대신하지 않는다.

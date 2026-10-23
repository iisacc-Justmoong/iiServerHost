cmake_minimum_required(VERSION 3.24)
project(iiWebServer VERSION 0.6.1 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(BUILD_SHARED_LIBS "Build shared libraries (disable for iOS)" ON)
option(IIWEBSERVER_BUILD_COMPATIBILITY "Build the existing Qt Core LAN/relay API and tools" ON)
set(IIWEBSERVER_NEEDS_CRYPTO FALSE)
if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME})
        set(_user_home "$ENV{HOME}")
    elseif(DEFINED ENV{USERPROFILE})
        set(_user_home "$ENV{USERPROFILE}")
    else()
        message(FATAL_ERROR "Set CMAKE_INSTALL_PREFIX explicitly: no user home is available")
    endif()
    set(CMAKE_INSTALL_PREFIX "${_user_home}/.local/SDK/iiWebServer" CACHE PATH "Install prefix" FORCE)
endif()

find_package(iiNetwork 0.5 CONFIG REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../iiNetwork/build/package" "$ENV{HOME}/.local/SDK/iiNetwork")
if(NOT iiNetwork_HTTP_SERVER_LIFECYCLE AND NOT TARGET iiNetwork)
    message(FATAL_ERROR "Rebuild iiNetwork with HTTP shutdown and response deadlines before building iiWebServer")
endif()
add_library(iiWebServerHttp src/HttpServer.cpp src/iiWebServer/HttpServer.h src/iiWebServer/HttpExport.h)
add_library(iiWebServer::Http ALIAS iiWebServerHttp)
set_target_properties(iiWebServerHttp PROPERTIES EXPORT_NAME Http CXX_EXTENSIONS OFF
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES INSTALL_RPATH_USE_LINK_PATH TRUE)
target_compile_features(iiWebServerHttp PUBLIC cxx_std_23)
target_compile_definitions(iiWebServerHttp PRIVATE IIWEBSERVER_HTTP_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiWebServerHttp PUBLIC IIWEBSERVER_HTTP_STATIC)
endif()
target_link_libraries(iiWebServerHttp PUBLIC iiNetwork::iiNetwork)
target_include_directories(iiWebServerHttp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(APPLE)
    set_property(TARGET iiWebServerHttp PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET iiWebServerHttp PROPERTY INSTALL_RPATH "$ORIGIN")
endif()
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(ii-http-server tools/http.cpp)
    target_link_libraries(ii-http-server PRIVATE iiWebServer::Http)
    set_target_properties(ii-http-server PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_EXTENSIONS OFF INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set_property(TARGET ii-http-server PROPERTY INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_property(TARGET ii-http-server PROPERTY INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS ii-http-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BUILD_TESTING)
    add_executable(iiWebServerHttpTests tests/http.cpp)
    target_link_libraries(iiWebServerHttpTests PRIVATE iiWebServer::Http)
    set_target_properties(iiWebServerHttpTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_EXTENSIONS OFF)
    foreach(_case routing protocol lifetime)
        add_test(NAME iiWebServer.http.${_case} COMMAND iiWebServerHttpTests ${_case})
        set_tests_properties(iiWebServer.http.${_case} PROPERTIES TIMEOUT 20)
    endforeach()
    if(TARGET ii-http-server)
        find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
        add_test(NAME iiWebServer.http.cli COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_cli.py" "$<TARGET_FILE:ii-http-server>" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(iiWebServer.http.cli PROPERTIES TIMEOUT 45)
        set(IIWEBSERVER_TEST_NGINX "" CACHE FILEPATH "Optional nginx binary for real TLS reverse-proxy integration")
        if(IIWEBSERVER_TEST_NGINX)
            get_filename_component(_http_openssl_prefix "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
            find_program(IIWEBSERVER_TEST_OPENSSL_COMMAND NAMES openssl HINTS "${_http_openssl_prefix}/bin" REQUIRED)
            find_program(IIWEBSERVER_TEST_CURL_COMMAND NAMES curl REQUIRED)
            add_test(NAME iiWebServer.http.nginx COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/nginx_http.py" "${IIWEBSERVER_TEST_NGINX}"
                "$<TARGET_FILE:ii-http-server>" "${CMAKE_CURRENT_BINARY_DIR}"
                "${IIWEBSERVER_TEST_OPENSSL_COMMAND}" "${IIWEBSERVER_TEST_CURL_COMMAND}")
            set_tests_properties(iiWebServer.http.nginx PROPERTIES TIMEOUT 120)
        endif()
    endif()
endif()
install(TARGETS iiWebServerHttp EXPORT iiWebServerHttpTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/iiWebServer/HttpServer.h src/iiWebServer/HttpExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiWebServer)
install(EXPORT iiWebServerHttpTargets FILE iiWebServerHttpTargets.cmake NAMESPACE iiWebServer:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer)
install(FILES docs/HttpServer.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/docs)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer)
install(FILES deploy/nginx/iiwebserver.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/nginx)

if(IIWEBSERVER_BUILD_COMPATIBILITY)
# File creation is owned by iiFileProvider.
find_package(iiFileProvider 0.7 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiFileProviderNative 0.7 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")






find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)
set(CMAKE_AUTOMOC ON)

add_library(iiWebServer src/iiWebServer.h src/iiWebServer.cpp src/WebServer.h src/iiWebServerExport.h
    src/NetworkTransport.h src/NetworkTransport.cpp src/NetworkTypes.h
    src/Protocol.h src/BinaryFrame.h src/SessionAuthenticator.cpp src/RelayServer.cpp src/Peer.cpp src/FileShare.cpp src/PairingLink.cpp src/PairingLink.h src/LanPeer.cpp src/LanPeer.h
    src/Transfer.h src/Transfer.cpp src/StorageBridge.h src/StorageBridge.cpp)
set(_transfer_default ON)
if(IOS OR ANDROID)
    set(_transfer_default OFF)
endif()
option(IIWEBSERVER_WITH_CURL "Enable standard file-transfer protocols using libcurl >= 7.85" ${_transfer_default})
option(IIWEBSERVER_WITH_STORAGE_PROCESS "Enable external rclone processes (desktop/server only)" ${_transfer_default})
if(IIWEBSERVER_WITH_STORAGE_PROCESS)
    if(IOS OR ANDROID)
        message(FATAL_ERROR "External storage processes are not supported by the mobile SDK")
    endif()
    target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_STORAGE_PROCESS=1)
    if(WIN32)
        target_link_libraries(iiWebServer PRIVATE advapi32)
    endif()
endif()
if(IIWEBSERVER_WITH_CURL)
    find_package(CURL 7.85 REQUIRED)
    target_link_libraries(iiWebServer PRIVATE CURL::libcurl)
    target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_CURL=1)
endif()
if(NOT IOS AND NOT ANDROID)
    configure_file(tools/ftp_server.py "${CMAKE_CURRENT_BINARY_DIR}/ftp_server.py" COPYONLY)
    configure_file(tools/provider_fs.py "${CMAKE_CURRENT_BINARY_DIR}/provider_fs.py" COPYONLY)
    file(COPY "${iiFileProviderNative_PYTHON_DIRECTORY}/ii_file_provider"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
        FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
    install(FILES tools/ftp_server.py tools/provider_fs.py tools/ftp-requirements.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer)
    install(DIRECTORY "${iiFileProviderNative_PYTHON_DIRECTORY}/ii_file_provider"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer
        FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
    get_target_property(_provider_kind iiFileProvider::Native TYPE)
    get_target_property(_provider_imported iiFileProvider::Native IMPORTED)
    if(_provider_kind STREQUAL "SHARED_LIBRARY")
        add_custom_target(iiWebServerProviderRuntime ALL
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:iiFileProvider::Native>"
                "${CMAKE_CURRENT_BINARY_DIR}/lib/$<TARGET_FILE_NAME:iiFileProvider::Native>"
            DEPENDS iiFileProvider::Native VERBATIM)
        if(WIN32)
            # ctypes resolves transitive dependencies beside the loaded DLL;
            # PATH entries alone are insufficient on current Python versions.
            get_target_property(_sqlite_library SQLite3::SQLite3 IMPORTED_LOCATION_RELEASE)
            if(NOT _sqlite_library)
                get_target_property(_sqlite_library SQLite3::SQLite3 IMPORTED_LOCATION)
            endif()
            if(_sqlite_library)
                get_filename_component(_sqlite_library_directory "${_sqlite_library}" DIRECTORY)
                find_file(IIWEBSERVER_SQLITE_RUNTIME NAMES sqlite3.dll
                    HINTS "${_sqlite_library_directory}/../bin" NO_DEFAULT_PATH)
            endif()
            if(IIWEBSERVER_SQLITE_RUNTIME)
                add_custom_command(TARGET iiWebServerProviderRuntime POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${IIWEBSERVER_SQLITE_RUNTIME}"
                        "${CMAKE_CURRENT_BINARY_DIR}/lib/sqlite3.dll" VERBATIM)
                install(FILES "${IIWEBSERVER_SQLITE_RUNTIME}" DESTINATION ${CMAKE_INSTALL_BINDIR})
            endif()
        endif()
        if(_provider_imported)
            install(IMPORTED_RUNTIME_ARTIFACTS iiFileProvider::Native
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()
    endif()
    install(DIRECTORY "${iiFileProviderNative_LICENSE_DIRECTORY}/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiFileProviderNative)
    set(IIWEBSERVER_NEEDS_CRYPTO TRUE)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    target_sources(iiWebServer PRIVATE src/LanIdentity.cpp src/LanIdentity.h)
    target_link_libraries(iiWebServer PRIVATE OpenSSL::Crypto)
    target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_LAN_HOST=1)
endif()
add_library(iiWebServer::iiWebServer ALIAS iiWebServer)
target_compile_features(iiWebServer PUBLIC cxx_std_23)
target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiWebServer PUBLIC IIWEBSERVER_STATIC)
endif()
target_link_libraries(iiWebServer PUBLIC Qt6::Core iiNetwork::iiNetwork iiWebServer::Http iiFileProvider::iiFileProvider iiFileProvider::Native)
target_include_directories(iiWebServer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(iiWebServer PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
if(APPLE)
    set_property(TARGET iiWebServer PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET iiWebServer PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

if(BUILD_TESTING)
    include(cmake/TestCertificate.cmake)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(iiWebServerLanTests tests/lan.cpp)
    target_link_libraries(iiWebServerLanTests PRIVATE iiWebServer::iiWebServer Qt6::Test)
    target_compile_definitions(iiWebServerLanTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiWebServer.lan COMMAND iiWebServerLanTests)
    set_tests_properties(iiWebServer.lan PROPERTIES TIMEOUT 45)
    add_executable(iiWebServerTests tests/hosting.cpp tests/NetworkFixture.h)
    target_link_libraries(iiWebServerTests PRIVATE iiWebServer::iiWebServer Qt6::Test)
    target_compile_definitions(iiWebServerTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiWebServer.hosting COMMAND iiWebServerTests)
    add_executable(iiWebServerPairingTests tests/pairing.cpp)
    target_link_libraries(iiWebServerPairingTests PRIVATE iiWebServer::iiWebServer Qt6::Test)
    add_test(NAME iiWebServer.pairing COMMAND iiWebServerPairingTests)
    set_tests_properties(iiWebServer.pairing PROPERTIES TIMEOUT 45)
    set_tests_properties(iiWebServer.hosting PROPERTIES TIMEOUT 60)
    add_executable(iiWebServerHelloWorld tests/hello_world.cpp)
    target_link_libraries(iiWebServerHelloWorld PRIVATE iiWebServer::iiWebServer)
    set_target_properties(iiWebServerHelloWorld PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME iiWebServer.hello_world COMMAND iiWebServerHelloWorld)
    add_executable(iiWebServerTransferTests tests/transfer.cpp tests/NetworkFixture.h)
    target_link_libraries(iiWebServerTransferTests PRIVATE iiWebServer::iiWebServer Qt6::Test)
    target_compile_definitions(iiWebServerTransferTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiWebServer.transfer COMMAND iiWebServerTransferTests)
    set_tests_properties(iiWebServer.transfer PROPERTIES TIMEOUT 45)
    add_executable(iiWebServerOptionalBackendTests tests/optional_backends.cpp)
    target_link_libraries(iiWebServerOptionalBackendTests PRIVATE iiWebServer::iiWebServer Qt6::Test)
    target_compile_definitions(iiWebServerOptionalBackendTests PRIVATE
        TEST_CURL_ENABLED=$<BOOL:${IIWEBSERVER_WITH_CURL}> TEST_PROCESS_ENABLED=$<BOOL:${IIWEBSERVER_WITH_STORAGE_PROCESS}>)
    add_test(NAME iiWebServer.optional_backends COMMAND iiWebServerOptionalBackendTests)
    if(IIWEBSERVER_WITH_STORAGE_PROCESS)
        add_executable(iiWebServerStorageTests tests/storage_bridge.cpp)
        target_link_libraries(iiWebServerStorageTests PRIVATE iiWebServer::iiWebServer Qt6::Test)
        target_compile_definitions(iiWebServerStorageTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME iiWebServer.storage COMMAND iiWebServerStorageTests)
        set_tests_properties(iiWebServer.storage PROPERTIES TIMEOUT 180)
    endif()
endif()

if(NOT IOS AND NOT ANDROID)
    add_executable(ii-file-transfer tools/transfer.cpp)
    add_executable(ii-storage tools/storage.cpp)
    foreach(_tool ii-file-transfer ii-storage)
        target_link_libraries(${_tool} PRIVATE iiWebServer::iiWebServer)
        set_target_properties(${_tool} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
        if(APPLE)
            set_target_properties(${_tool} PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
        elseif(UNIX)
            set_target_properties(${_tool} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        endif()
        install(TARGETS ${_tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()
    add_executable(ii-server-relay tools/relay.cpp)
    target_link_libraries(ii-server-relay PRIVATE iiWebServer::iiWebServer)
    add_executable(ii-web-server tools/host.cpp)
    target_link_libraries(ii-web-server PRIVATE iiWebServer::iiWebServer)
    set_target_properties(ii-web-server PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set_target_properties(ii-web-server ii-server-relay PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(ii-web-server ii-server-relay PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS ii-web-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set_target_properties(ii-server-relay PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    install(TARGETS ii-server-relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(_smb_fixture_arguments)
        if(WIN32)
            find_program(IIWEBSERVER_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
            set(_smb_fixture_directory "${CMAKE_CURRENT_BINARY_DIR}/smb-fixture")
            set(_smb_fixture_project "${CMAKE_CURRENT_SOURCE_DIR}/tests/SmbWindowsFixture/SmbWindowsFixture.csproj")
            set(_smb_fixture_dll "${_smb_fixture_directory}/SmbWindowsFixture.dll")
            add_custom_command(OUTPUT "${_smb_fixture_dll}"
                COMMAND "${CMAKE_COMMAND}" -E env
                    "DOTNET_CLI_HOME=${_smb_fixture_directory}/home"
                    "NUGET_PACKAGES=${_smb_fixture_directory}/packages"
                    DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false
                    "${Python3_EXECUTABLE}" -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/SmbWindowsFixture/prepare_source.py"
                    "${_smb_fixture_directory}"
                COMMAND "${CMAKE_COMMAND}" -E env
                    "DOTNET_CLI_HOME=${_smb_fixture_directory}/home"
                    "NUGET_PACKAGES=${_smb_fixture_directory}/packages"
                    DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false
                    "${IIWEBSERVER_DOTNET_EXECUTABLE}" build "${_smb_fixture_project}"
                    --configuration Release --output "${_smb_fixture_directory}" --nologo
                    "-p:BaseIntermediateOutputPath=${_smb_fixture_directory}/obj/"
                    "-p:SMBSourceRoot=${_smb_fixture_directory}/SMBLibrary-582635541d2ec0c84eac5f66cbebd41e2d35c7ae"
                DEPENDS "${_smb_fixture_project}"
                    tests/SmbWindowsFixture/Program.cs tests/SmbWindowsFixture/prepare_source.py
                VERBATIM)
            add_custom_target(iiWebServerSmbFixture ALL DEPENDS "${_smb_fixture_dll}")
            set(_smb_fixture_arguments "${IIWEBSERVER_DOTNET_EXECUTABLE}" "${_smb_fixture_dll}")
        endif()
        add_test(NAME iiWebServer.provider_ftp COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/provider_ftp.py" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(iiWebServer.provider_ftp PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_test(NAME iiWebServer.provider_ftps COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/provider_ftp.py" "${CMAKE_CURRENT_BINARY_DIR}" --implicit)
        set_tests_properties(iiWebServer.provider_ftps PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_test(NAME iiWebServer.fixture_cleanup COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture_directory.py")
        add_test(NAME iiWebServer.protocol_processes COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_protocols.py" "$<TARGET_FILE:ii-file-transfer>"
            "$<TARGET_FILE:ii-storage>" "${CMAKE_CURRENT_BINARY_DIR}" ${_smb_fixture_arguments})
        set_tests_properties(iiWebServer.protocol_processes PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
        add_test(NAME iiWebServer.process_hosting COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_hosting.py" "$<TARGET_FILE:ii-server-relay>"
            "$<TARGET_FILE:ii-web-server>" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(iiWebServer.process_hosting PROPERTIES TIMEOUT 45)
    endif()
endif()

install(TARGETS iiWebServer EXPORT iiWebServerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES src/NetworkTypes.h src/NetworkTransport.h src/iiWebServer.h src/WebServer.h src/PairingLink.h src/LanPeer.h src/Transfer.h src/StorageBridge.h src/iiWebServerExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES docs/Network.md docs/FileTransfer.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/docs)
install(FILES ThirdParty/Curl-LICENSE.txt ThirdParty/Rclone-LICENSE.txt
    ThirdParty/Pyftpdlib-LICENSE.txt ThirdParty/PyOpenSSL-LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/licenses)
if(IIWEBSERVER_NEEDS_CRYPTO)
    install(FILES ThirdParty/OpenSSL-LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/licenses)
endif()
install(EXPORT iiWebServerTargets
    FILE iiWebServerTargets.cmake
    NAMESPACE iiWebServer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer
)
else()
    set(IIWEBSERVER_WITH_CURL OFF)
    set(IIWEBSERVER_WITH_STORAGE_PROCESS OFF)
endif()

configure_package_config_file(cmake/iiWebServerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer
)

if(APPLE AND BUILD_TESTING AND IIWEBSERVER_BUILD_COMPATIBILITY)
    get_property(_web_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_web_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:" "DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()

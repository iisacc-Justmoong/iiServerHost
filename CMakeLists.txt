cmake_minimum_required(VERSION 3.24)
project(iiServerHost VERSION 0.6.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME})
        set(_user_home "$ENV{HOME}")
    elseif(DEFINED ENV{USERPROFILE})
        set(_user_home "$ENV{USERPROFILE}")
    else()
        message(FATAL_ERROR "Set CMAKE_INSTALL_PREFIX explicitly: no user home is available")
    endif()
    set(CMAKE_INSTALL_PREFIX "${_user_home}/.local/SDK/iiServerHost" CACHE PATH "Install prefix" FORCE)
endif()

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Network WebSockets)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(BUILD_SHARED_LIBS "Build a shared SDK (disable for iOS)" ON)
add_library(iiServerHost src/iiServerHost.h src/iiServerHost.cpp src/ServerHost.h src/iiServerHostExport.h
    src/Protocol.h src/BinaryFrame.h src/SessionAuthenticator.cpp src/RelayServer.cpp src/Peer.cpp src/FileShare.cpp src/PairingLink.cpp src/PairingLink.h src/LanPeer.cpp src/LanPeer.h
    src/Transfer.h src/Transfer.cpp src/StorageBridge.h src/StorageBridge.cpp)
set(_transfer_default ON)
if(IOS OR ANDROID)
    set(_transfer_default OFF)
endif()
option(IISERVERHOST_WITH_CURL "Enable standard file-transfer protocols using libcurl >= 7.85" ${_transfer_default})
option(IISERVERHOST_WITH_STORAGE_PROCESS "Enable external rclone processes (desktop/server only)" ${_transfer_default})
if(IISERVERHOST_WITH_STORAGE_PROCESS)
    if(IOS OR ANDROID)
        message(FATAL_ERROR "External storage processes are not supported by the mobile SDK")
    endif()
    target_compile_definitions(iiServerHost PRIVATE IISERVERHOST_STORAGE_PROCESS=1)
endif()
if(IISERVERHOST_WITH_CURL)
    find_package(CURL 7.85 REQUIRED)
    target_link_libraries(iiServerHost PRIVATE CURL::libcurl)
    target_compile_definitions(iiServerHost PRIVATE IISERVERHOST_CURL=1)
endif()
if(NOT IOS AND NOT ANDROID)
    configure_file(tools/ftp_server.py "${CMAKE_CURRENT_BINARY_DIR}/ftp_server.py" COPYONLY)
    install(FILES tools/ftp_server.py tools/ftp-requirements.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiServerHost)
    set(IISERVERHOST_NEEDS_CRYPTO TRUE)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    target_sources(iiServerHost PRIVATE src/LanIdentity.cpp src/LanIdentity.h)
    target_link_libraries(iiServerHost PRIVATE OpenSSL::Crypto)
    target_compile_definitions(iiServerHost PRIVATE IISERVERHOST_LAN_HOST=1)
endif()
add_library(iiServerHost::iiServerHost ALIAS iiServerHost)
target_compile_features(iiServerHost PUBLIC cxx_std_20)
target_compile_definitions(iiServerHost PRIVATE IISERVERHOST_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiServerHost PUBLIC IISERVERHOST_STATIC)
endif()
target_link_libraries(iiServerHost PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)
target_include_directories(iiServerHost PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(iiServerHost PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

if(BUILD_TESTING)
    include(cmake/TestCertificate.cmake)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(iiServerHostLanTests tests/lan.cpp)
    target_link_libraries(iiServerHostLanTests PRIVATE iiServerHost::iiServerHost Qt6::Test)
    target_compile_definitions(iiServerHostLanTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiServerHost.lan COMMAND iiServerHostLanTests)
    set_tests_properties(iiServerHost.lan PROPERTIES TIMEOUT 45)
    add_executable(iiServerHostTests tests/hosting.cpp)
    target_link_libraries(iiServerHostTests PRIVATE iiServerHost::iiServerHost Qt6::Test)
    target_compile_definitions(iiServerHostTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiServerHost.hosting COMMAND iiServerHostTests)
    add_executable(iiServerHostPairingTests tests/pairing.cpp)
    target_link_libraries(iiServerHostPairingTests PRIVATE iiServerHost::iiServerHost Qt6::Test)
    add_test(NAME iiServerHost.pairing COMMAND iiServerHostPairingTests)
    set_tests_properties(iiServerHost.pairing PROPERTIES TIMEOUT 45)
    set_tests_properties(iiServerHost.hosting PROPERTIES TIMEOUT 60)
    add_executable(iiServerHostHelloWorld tests/hello_world.cpp)
    target_link_libraries(iiServerHostHelloWorld PRIVATE iiServerHost::iiServerHost)
    set_target_properties(iiServerHostHelloWorld PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME iiServerHost.hello_world COMMAND iiServerHostHelloWorld)
    add_executable(iiServerHostTransferTests tests/transfer.cpp)
    target_link_libraries(iiServerHostTransferTests PRIVATE iiServerHost::iiServerHost Qt6::Test)
    target_compile_definitions(iiServerHostTransferTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiServerHost.transfer COMMAND iiServerHostTransferTests)
    set_tests_properties(iiServerHost.transfer PROPERTIES TIMEOUT 45)
    add_executable(iiServerHostOptionalBackendTests tests/optional_backends.cpp)
    target_link_libraries(iiServerHostOptionalBackendTests PRIVATE iiServerHost::iiServerHost Qt6::Test)
    target_compile_definitions(iiServerHostOptionalBackendTests PRIVATE
        TEST_CURL_ENABLED=$<BOOL:${IISERVERHOST_WITH_CURL}> TEST_PROCESS_ENABLED=$<BOOL:${IISERVERHOST_WITH_STORAGE_PROCESS}>)
    add_test(NAME iiServerHost.optional_backends COMMAND iiServerHostOptionalBackendTests)
    if(IISERVERHOST_WITH_STORAGE_PROCESS)
        add_executable(iiServerHostStorageTests tests/storage_bridge.cpp)
        target_link_libraries(iiServerHostStorageTests PRIVATE iiServerHost::iiServerHost Qt6::Test)
        target_compile_definitions(iiServerHostStorageTests PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME iiServerHost.storage COMMAND iiServerHostStorageTests)
        set_tests_properties(iiServerHost.storage PROPERTIES TIMEOUT 180)
    endif()
endif()

if(NOT IOS AND NOT ANDROID)
    add_executable(ii-file-transfer tools/transfer.cpp)
    add_executable(ii-storage tools/storage.cpp)
    foreach(_tool ii-file-transfer ii-storage)
        target_link_libraries(${_tool} PRIVATE iiServerHost::iiServerHost)
        set_target_properties(${_tool} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
        if(APPLE)
            set_target_properties(${_tool} PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
        elseif(UNIX)
            set_target_properties(${_tool} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        endif()
        install(TARGETS ${_tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()
    add_executable(ii-server-relay tools/relay.cpp)
    target_link_libraries(ii-server-relay PRIVATE iiServerHost::iiServerHost)
    add_executable(ii-server-host tools/host.cpp)
    target_link_libraries(ii-server-host PRIVATE iiServerHost::iiServerHost)
    set_target_properties(ii-server-host PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set_target_properties(ii-server-host ii-server-relay PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(ii-server-host ii-server-relay PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS ii-server-host RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set_target_properties(ii-server-relay PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    install(TARGETS ii-server-relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME iiServerHost.protocol_processes COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_protocols.py" "$<TARGET_FILE:ii-file-transfer>"
            "$<TARGET_FILE:ii-storage>" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(iiServerHost.protocol_processes PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
        add_test(NAME iiServerHost.process_hosting COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_hosting.py" "$<TARGET_FILE:ii-server-relay>"
            "$<TARGET_FILE:ii-server-host>" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(iiServerHost.process_hosting PROPERTIES TIMEOUT 45)
    endif()
endif()

install(TARGETS iiServerHost EXPORT iiServerHostTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES src/iiServerHost.h src/ServerHost.h src/PairingLink.h src/LanPeer.h src/Transfer.h src/StorageBridge.h src/iiServerHostExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiServerHost)
install(FILES docs/FileTransfer.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiServerHost/docs)
install(FILES ThirdParty/Curl-LICENSE.txt ThirdParty/Rclone-LICENSE.txt
    ThirdParty/Pyftpdlib-LICENSE.txt ThirdParty/PyOpenSSL-LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiServerHost/licenses)
if(IISERVERHOST_NEEDS_CRYPTO)
    install(FILES ThirdParty/OpenSSL-LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiServerHost/licenses)
endif()
install(EXPORT iiServerHostTargets
    FILE iiServerHostTargets.cmake
    NAMESPACE iiServerHost::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiServerHost
)
configure_package_config_file(cmake/iiServerHostConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/iiServerHostConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiServerHost
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/iiServerHostConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/iiServerHostConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/iiServerHostConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiServerHost
)

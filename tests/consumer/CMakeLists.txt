cmake_minimum_required(VERSION 3.24)
project(iiWebServerConsumer LANGUAGES CXX)

find_package(iiWebServer 0.5.0 CONFIG REQUIRED)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
include(../../cmake/TestCertificate.cmake)
add_executable(iiWebServerConsumerHosting ../hosting.cpp ../NetworkFixture.h)
target_link_libraries(iiWebServerConsumerHosting PRIVATE iiWebServer::iiWebServer Qt6::Test)
target_compile_definitions(iiWebServerConsumerHosting PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
add_executable(iiWebServerConsumer ../hello_world.cpp)
target_link_libraries(iiWebServerConsumer PRIVATE iiWebServer::iiWebServer)
set_target_properties(iiWebServerConsumer PROPERTIES CXX_EXTENSIONS OFF)
enable_testing()
add_executable(iiWebServerConsumerLan ../lan.cpp)
target_link_libraries(iiWebServerConsumerLan PRIVATE iiWebServer::iiWebServer Qt6::Test)
target_compile_definitions(iiWebServerConsumerLan PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME iiWebServer.installed_lan COMMAND iiWebServerConsumerLan)
add_test(NAME iiWebServer.installed_consumer COMMAND iiWebServerConsumer)
add_test(NAME iiWebServer.installed_hosting COMMAND iiWebServerConsumerHosting)
add_executable(iiWebServerConsumerPairing ../pairing.cpp)
target_link_libraries(iiWebServerConsumerPairing PRIVATE iiWebServer::iiWebServer Qt6::Test)
add_test(NAME iiWebServer.installed_pairing COMMAND iiWebServerConsumerPairing)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(host_prefix "${iiWebServer_DIR}/../../.." ABSOLUTE)
add_executable(iiWebServerConsumerTransfer ../transfer.cpp ../NetworkFixture.h)
target_link_libraries(iiWebServerConsumerTransfer PRIVATE iiWebServer::iiWebServer Qt6::Test)
target_compile_definitions(iiWebServerConsumerTransfer PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME iiWebServer.installed_transfer COMMAND iiWebServerConsumerTransfer)
add_executable(iiWebServerConsumerOptionalBackends ../optional_backends.cpp)
target_link_libraries(iiWebServerConsumerOptionalBackends PRIVATE iiWebServer::iiWebServer Qt6::Test)
target_compile_definitions(iiWebServerConsumerOptionalBackends PRIVATE
    TEST_CURL_ENABLED=$<BOOL:${iiWebServer_CURL_ENABLED}> TEST_PROCESS_ENABLED=$<BOOL:${iiWebServer_STORAGE_PROCESS_ENABLED}>)
add_test(NAME iiWebServer.installed_optional_backends COMMAND iiWebServerConsumerOptionalBackends)
if(iiWebServer_STORAGE_PROCESS_ENABLED)
    add_executable(iiWebServerConsumerStorage ../storage_bridge.cpp)
    target_link_libraries(iiWebServerConsumerStorage PRIVATE iiWebServer::iiWebServer Qt6::Test)
    target_compile_definitions(iiWebServerConsumerStorage PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiWebServer.installed_storage COMMAND iiWebServerConsumerStorage)
    set_tests_properties(iiWebServer.installed_storage PROPERTIES TIMEOUT 180)
    configure_file("${host_prefix}/share/iiWebServer/ftp_server.py" "${CMAKE_CURRENT_BINARY_DIR}/ftp_server.py" COPYONLY)
    add_test(NAME iiWebServer.installed_protocol_processes COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/../process_protocols.py"
        "${host_prefix}/bin/ii-file-transfer${CMAKE_EXECUTABLE_SUFFIX}"
        "${host_prefix}/bin/ii-storage${CMAKE_EXECUTABLE_SUFFIX}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(iiWebServer.installed_protocol_processes PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77
        ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()
add_test(NAME iiWebServer.installed_process_hosting COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../process_hosting.py"
    "${host_prefix}/bin/ii-server-relay${CMAKE_EXECUTABLE_SUFFIX}"
    "${host_prefix}/bin/ii-web-server${CMAKE_EXECUTABLE_SUFFIX}"
    "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(iiWebServer.installed_process_hosting PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")
if(APPLE)
    get_property(_web_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_web_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:" "DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()

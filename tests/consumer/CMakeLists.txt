cmake_minimum_required(VERSION 3.24)
project(iiServerHostConsumer LANGUAGES CXX)

find_package(iiServerHost 0.5.0 CONFIG REQUIRED)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
include(../../cmake/TestCertificate.cmake)
add_executable(iiServerHostConsumerHosting ../hosting.cpp)
target_link_libraries(iiServerHostConsumerHosting PRIVATE iiServerHost::iiServerHost Qt6::Test)
target_compile_definitions(iiServerHostConsumerHosting PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
add_executable(iiServerHostConsumer ../hello_world.cpp)
target_link_libraries(iiServerHostConsumer PRIVATE iiServerHost::iiServerHost)
set_target_properties(iiServerHostConsumer PROPERTIES CXX_EXTENSIONS OFF)
enable_testing()
add_executable(iiServerHostConsumerLan ../lan.cpp)
target_link_libraries(iiServerHostConsumerLan PRIVATE iiServerHost::iiServerHost Qt6::Test)
target_compile_definitions(iiServerHostConsumerLan PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME iiServerHost.installed_lan COMMAND iiServerHostConsumerLan)
add_test(NAME iiServerHost.installed_consumer COMMAND iiServerHostConsumer)
add_test(NAME iiServerHost.installed_hosting COMMAND iiServerHostConsumerHosting)
add_executable(iiServerHostConsumerPairing ../pairing.cpp)
target_link_libraries(iiServerHostConsumerPairing PRIVATE iiServerHost::iiServerHost Qt6::Test)
add_test(NAME iiServerHost.installed_pairing COMMAND iiServerHostConsumerPairing)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(host_prefix "${iiServerHost_DIR}/../../.." ABSOLUTE)
add_executable(iiServerHostConsumerTransfer ../transfer.cpp)
target_link_libraries(iiServerHostConsumerTransfer PRIVATE iiServerHost::iiServerHost Qt6::Test)
target_compile_definitions(iiServerHostConsumerTransfer PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME iiServerHost.installed_transfer COMMAND iiServerHostConsumerTransfer)
add_executable(iiServerHostConsumerOptionalBackends ../optional_backends.cpp)
target_link_libraries(iiServerHostConsumerOptionalBackends PRIVATE iiServerHost::iiServerHost Qt6::Test)
target_compile_definitions(iiServerHostConsumerOptionalBackends PRIVATE
    TEST_CURL_ENABLED=$<BOOL:${iiServerHost_CURL_ENABLED}> TEST_PROCESS_ENABLED=$<BOOL:${iiServerHost_STORAGE_PROCESS_ENABLED}>)
add_test(NAME iiServerHost.installed_optional_backends COMMAND iiServerHostConsumerOptionalBackends)
if(iiServerHost_STORAGE_PROCESS_ENABLED)
    add_executable(iiServerHostConsumerStorage ../storage_bridge.cpp)
    target_link_libraries(iiServerHostConsumerStorage PRIVATE iiServerHost::iiServerHost Qt6::Test)
    target_compile_definitions(iiServerHostConsumerStorage PRIVATE TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiServerHost.installed_storage COMMAND iiServerHostConsumerStorage)
    set_tests_properties(iiServerHost.installed_storage PROPERTIES TIMEOUT 180)
    configure_file("${host_prefix}/share/iiServerHost/ftp_server.py" "${CMAKE_CURRENT_BINARY_DIR}/ftp_server.py" COPYONLY)
    add_test(NAME iiServerHost.installed_protocol_processes COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/../process_protocols.py"
        "${host_prefix}/bin/ii-file-transfer${CMAKE_EXECUTABLE_SUFFIX}"
        "${host_prefix}/bin/ii-storage${CMAKE_EXECUTABLE_SUFFIX}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(iiServerHost.installed_protocol_processes PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77
        ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()
add_test(NAME iiServerHost.installed_process_hosting COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../process_hosting.py"
    "${host_prefix}/bin/ii-server-relay${CMAKE_EXECUTABLE_SUFFIX}"
    "${host_prefix}/bin/ii-server-host${CMAKE_EXECUTABLE_SUFFIX}"
    "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(iiServerHost.installed_process_hosting PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")

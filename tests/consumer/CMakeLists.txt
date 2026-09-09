cmake_minimum_required(VERSION 3.24)
project(iiServerHostConsumer LANGUAGES CXX)

find_package(iiServerHost 0.3.0 CONFIG REQUIRED)
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
add_test(NAME iiServerHost.installed_consumer COMMAND iiServerHostConsumer)
add_test(NAME iiServerHost.installed_hosting COMMAND iiServerHostConsumerHosting)
add_executable(iiServerHostConsumerPairing ../pairing.cpp)
target_link_libraries(iiServerHostConsumerPairing PRIVATE iiServerHost::iiServerHost Qt6::Test)
add_test(NAME iiServerHost.installed_pairing COMMAND iiServerHostConsumerPairing)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(host_prefix "${iiServerHost_DIR}/../../.." ABSOLUTE)
add_test(NAME iiServerHost.installed_process_hosting COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../process_hosting.py"
    "${host_prefix}/bin/ii-server-relay${CMAKE_EXECUTABLE_SUFFIX}"
    "${host_prefix}/bin/ii-server-host${CMAKE_EXECUTABLE_SUFFIX}"
    "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(iiServerHost.installed_process_hosting PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")

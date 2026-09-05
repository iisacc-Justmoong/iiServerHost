cmake_minimum_required(VERSION 3.24)
project(iiServerHostConsumer LANGUAGES CXX)

find_package(iiServerHost 0.1.0 CONFIG REQUIRED)
add_executable(iiServerHostConsumer ../hello_world.cpp)
target_link_libraries(iiServerHostConsumer PRIVATE iiServerHost::iiServerHost)
set_target_properties(iiServerHostConsumer PROPERTIES CXX_EXTENSIONS OFF)
enable_testing()
add_test(NAME iiServerHost.installed_consumer COMMAND iiServerHostConsumer)

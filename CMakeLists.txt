cmake_minimum_required(VERSION 3.24)
project(iiServerHost VERSION 0.1.0 LANGUAGES CXX)

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

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

add_library(iiServerHost SHARED iiServerHost.h iiServerHost.cpp)
add_library(iiServerHost::iiServerHost ALIAS iiServerHost)
target_compile_features(iiServerHost PUBLIC cxx_std_20)
target_compile_definitions(iiServerHost PRIVATE IISERVERHOST_BUILDING_LIBRARY)
target_link_libraries(iiServerHost PUBLIC Qt6::Core)
target_include_directories(iiServerHost PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
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
    add_executable(iiServerHostHelloWorld tests/hello_world.cpp)
    target_link_libraries(iiServerHostHelloWorld PRIVATE iiServerHost::iiServerHost)
    set_target_properties(iiServerHostHelloWorld PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME iiServerHost.hello_world COMMAND iiServerHostHelloWorld)
endif()

install(TARGETS iiServerHost EXPORT iiServerHostTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES iiServerHost.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiServerHost)
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

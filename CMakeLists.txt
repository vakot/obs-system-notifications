cmake_minimum_required(VERSION 3.28...3.30)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "obs-system-notifications supports Windows and Linux")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(OBS_DEV_DIR "" CACHE PATH "Portable OBS directory used by the local install target")

include(compilerconfig)
include(defaults)

if(OBS_DEV_DIR AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${OBS_DEV_DIR}" CACHE PATH "Install prefix" FORCE)
endif()

set(_platform_sources)
if(WIN32)
  list(APPEND _platform_sources src/notifications/platform/windows.cpp)
else()
  list(APPEND _platform_sources src/notifications/platform/linux.cpp)
  find_package(PkgConfig 1.0 REQUIRED)
  find_package(Threads REQUIRED)
  pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
endif()

add_library(
  ${CMAKE_PROJECT_NAME}
  MODULE
    src/notifications/index.cpp
    ${_platform_sources}
    src/obs/event-router/index.cpp
    src/plugin-main.cpp
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE windowsapp shell32 propsys)
else()
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::DBUS Threads::Threads)
endif()

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES
    PREFIX ""
    VERSION 0
    SOVERSION ${PLUGIN_VERSION}
    OUTPUT_NAME ${_name}
)

if(WIN32)
  set(_plugin_destination "obs-plugins/64bit")
else()
  set(_plugin_destination "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
endif()
install(
  TARGETS ${CMAKE_PROJECT_NAME}
  RUNTIME DESTINATION "${_plugin_destination}"
  LIBRARY DESTINATION "${_plugin_destination}"
)
if(WIN32)
  install(
    FILES "$<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION "${_plugin_destination}"
    OPTIONAL
  )
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(
    obs-system-notifications-notification-test
    tests/notification-test.cpp
    src/notifications/index.cpp
  )
  target_include_directories(obs-system-notifications-notification-test PRIVATE src)
  add_test(NAME notification-definition COMMAND obs-system-notifications-notification-test)
endif()

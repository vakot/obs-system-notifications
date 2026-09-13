# CMake operating system bootstrap module

include_guard(GLOBAL)

if(CMAKE_SYSTEM_NAME)
  set(_osconfig_system_name "${CMAKE_SYSTEM_NAME}")
else()
  set(_osconfig_system_name "${CMAKE_HOST_SYSTEM_NAME}")
endif()

if(_osconfig_system_name STREQUAL "Windows")
  set(CMAKE_C_EXTENSIONS FALSE)
  set(CMAKE_CXX_EXTENSIONS FALSE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows")
  set(OS_WINDOWS TRUE)
elseif(_osconfig_system_name STREQUAL "Darwin")
  set(CMAKE_C_EXTENSIONS FALSE)
  set(CMAKE_CXX_EXTENSIONS FALSE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos")
  set(OS_MACOS TRUE)
elseif(_osconfig_system_name MATCHES "Linux|FreeBSD|OpenBSD")
  set(CMAKE_CXX_EXTENSIONS FALSE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linux")
  string(TOUPPER "${_osconfig_system_name}" _SYSTEM_NAME_U)
  set(OS_${_SYSTEM_NAME_U} TRUE)
endif()

unset(_osconfig_system_name)

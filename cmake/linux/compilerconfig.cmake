# CMake Linux compiler configuration module

include_guard(GLOBAL)

include(compiler_common)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

cmake_minimum_required(VERSION 4.0)
project(socks5_proxy VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build test binary (test.cc)" OFF)
option(WITH_SANITIZERS "Enable Address/Undefined sanitizer flags" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

file(GLOB_RECURSE PROXY_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/*.cc
)

add_executable(proxy ${PROXY_SOURCES})
find_library(SYSTEMD_LIB systemd)
if (SYSTEMD_LIB)
  target_link_libraries(proxy PRIVATE ${SYSTEMD_LIB})
else()
  message(FATAL_ERROR "libsystemd not found; install libsystemd-dev")
endif()
target_include_directories(proxy PRIVATE ${CMAKE_SOURCE_DIR}/includes)
target_compile_features(proxy PRIVATE cxx_std_23)

if(WITH_SANITIZERS)
  if(NOT MSVC)
    target_compile_options(proxy PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(proxy PRIVATE -fsanitize=address,undefined)
  endif()
endif()

set_target_properties(proxy PROPERTIES OUTPUT_NAME socks5_proxy)

install(TARGETS proxy RUNTIME DESTINATION bin)

if(BUILD_TESTS)
  add_executable(test_runner ${CMAKE_SOURCE_DIR}/test.cc)
  target_include_directories(test_runner PRIVATE ${CMAKE_SOURCE_DIR}/includes)
  target_compile_features(test_runner PRIVATE cxx_std_23)
  install(TARGETS test_runner RUNTIME DESTINATION bin)
endif()
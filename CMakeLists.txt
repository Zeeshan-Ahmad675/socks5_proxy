cmake_minimum_required(VERSION 4.3)
set(CMAKE_CXX_COMPILER clang++)
project(SOCKS5_PROXY)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS off)

add_executable(proxy src/epoll_util.cc src/fd_util.cc src/main.cc src/socks.cc src/tcp.cc)

target_include_directories(proxy PRIVATE includes)
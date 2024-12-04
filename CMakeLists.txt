cmake_minimum_required(VERSION 3.25)
project(iocp)
set(CMAKE_CXX_STANDARD 23)

add_library(iocp src/iocp.cpp include/iocp.h src/io_uring_operation_allocator.hpp src/wsa.cpp)
target_include_directories(iocp PUBLIC include)
target_link_libraries(iocp uring)

add_subdirectory(test)

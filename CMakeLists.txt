cmake_minimum_required(VERSION 3.25)
project(iocp)
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if (WIN32)
add_library(iocp INTERFACE)
target_include_directories(iocp INTERFACE include)
target_link_libraries(iocp INTERFACE ws2_32 kernel32 mswsock)
else()
add_library(iocp src/iocp.cpp include/iocp.h src/io_uring_operation_allocator.hpp src/wsa.cpp)
target_include_directories(iocp PUBLIC include)
target_link_libraries(iocp uring)
endif()

add_subdirectory(test)

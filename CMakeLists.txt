cmake_minimum_required(VERSION 3.25)
project(iocp)
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(IOUring)
link_libraries(${IOUring_LIBRARY})

if (NOT WIN32)
add_subdirectory(iocp4linux)
endif()

add_subdirectory(µasync)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    add_subdirectory(example)
endif()

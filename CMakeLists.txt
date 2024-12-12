cmake_minimum_required(VERSION 3.25)
project(iocp)
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(WITHOUT_IOURING "use epoll/poll/kqueue to support IOCP. not io_uring" OFF)

if (NOT WIN32)

    find_package(IOUring)

    if (IOUring_FOUND AND NOT WITHOUT_IOURING)

        link_libraries(${IOUring_LIBRARY})
        add_subdirectory(iocp4linux)

    else(IOUring_FOUND AND NOT WITHOUT_IOURING)

        add_subdirectory(iocp_asio)

    endif(IOUring_FOUND AND NOT WITHOUT_IOURING)

endif()

add_subdirectory(µasync)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    add_subdirectory(example)
endif()

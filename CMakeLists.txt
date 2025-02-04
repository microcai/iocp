cmake_minimum_required(VERSION 3.25)
project(iocp)
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_ASM_MASM_CREATE_STATIC_LIBRARY "<CMAKE_AR> /OUT:<TARGET> <LINK_FLAGS> <OBJECTS>")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(WITHOUT_IOURING "use epoll/poll/kqueue to support IOCP. not io_uring" OFF)

if (NOT APPLE)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-ffunction-sections -fdata-sections)
        add_link_options(-Wl,--gc-sections)
    endif()
endif()

if (NOT WIN32)

    find_package(IOUring)

    if (IOUring_FOUND AND NOT WITHOUT_IOURING)

        link_libraries(${IOUring_LIBRARY})
        add_subdirectory(iocp4linux)

    else(IOUring_FOUND AND NOT WITHOUT_IOURING)

        add_subdirectory(iocp_asio)

    endif(IOUring_FOUND AND NOT WITHOUT_IOURING)

endif()

add_subdirectory(uasync)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    add_subdirectory(example)
endif()

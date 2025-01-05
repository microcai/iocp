cmake_minimum_required(VERSION 3.30)
project(uasync)

option(DISABLE_BOOST_CONTEXT "disable the usa of boost.context asm" OFF)
option(DISABLE_UCONTEXT "disable ucontext and use setjmp/longjmp" OFF)
option(USE_ZCONTEXT "use zcontext api to do context switch" OFF)

add_library(uasync INTERFACE include/awaitable.hpp include/extensable_iocp.hpp include/iocp_callback.hpp include/universal_async.hpp include/universal_fiber.hpp include/universal_fiber.h)

target_include_directories(uasync INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (WIN32)
target_link_libraries(uasync INTERFACE ws2_32)
else()
target_link_libraries(uasync INTERFACE iocp4linux)
endif()

set_property(TARGET uasync PROPERTY CXX_STANDARD 23)

if (DISABLE_UCONTEXT)
target_compile_definitions(uasync INTERFACE -DDISABLE_UCONTEXT=1)
endif()

if (USE_ZCONTEXT)
    add_subdirectory(zcontext_asm)
    target_compile_definitions(uasync INTERFACE -DUSE_ZCONTEXT=1)
    target_link_libraries(uasync INTERFACE zcontext)
elseif (NOT DISABLE_BOOST_CONTEXT)
    add_subdirectory(fcontext_asm)
    target_compile_definitions(uasync INTERFACE -DUSE_BOOST_CONTEXT=1)
    target_link_libraries(uasync INTERFACE fcontext)
endif()

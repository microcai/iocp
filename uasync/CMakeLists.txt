cmake_minimum_required(VERSION 3.30)
project(uasync)

if (CMAKE_GENERATOR MATCHES "Ninja")
set(CMAKE_ASM_MASM_CREATE_STATIC_LIBRARY "<CMAKE_AR> /OUT:<TARGET> <LINK_FLAGS> <OBJECTS>")
endif()

option(DISABLE_BOOST_CONTEXT "disable the usa of boost.context asm" OFF)
option(DISABLE_UCONTEXT "disable ucontext and use setjmp/longjmp" OFF)
option(USE_ZCONTEXT "use zcontext api to do context switch" OFF)

add_library(uasync INTERFACE)

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
    if(MSVC)
        enable_language(ASM_MASM)
        add_library(zcontext asm/zcontext_x86_64.asm)
    else()
        enable_language(ASM)

        if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            add_library(zcontext asm/zcontext_arm64.S)
        else()
            add_library(zcontext asm/zcontext_x86_64.S)
        endif()
    endif()
    target_compile_definitions(uasync INTERFACE -DUSE_ZCONTEXT=1)
    target_link_libraries(uasync INTERFACE zcontext)
elseif (NOT DISABLE_BOOST_CONTEXT)
    add_subdirectory(asm)
    target_compile_definitions(uasync INTERFACE -DUSE_BOOST_CONTEXT=1)
    target_link_libraries(uasync INTERFACE fcontext)
endif()



option(DISABLE_BOOST_CONTEXT "disable the usa of boost.context asm" OFF)
option(DISABLE_UCONTEXT "disable ucontext and use setjmp/longjmp" OFF)
option(USE_ZCONTEXT "use zcontext api to do context switch" OFF)

add_library(uasync INTERFACE)

target_include_directories(uasync INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NOT WIN32)
target_link_libraries(uasync INTERFACE iocp4linux)
endif()

set_property(TARGET uasync PROPERTY CXX_STANDARD 23)

if (NOT DISABLE_BOOST_CONTEXT AND NOT WIN32)
    add_subdirectory(asm)
    target_compile_definitions(uasync INTERFACE -DUSE_BOOST_CONTEXT=1)
    target_link_libraries(uasync INTERFACE fcontext)
endif()

if (DISABLE_UCONTEXT)
target_compile_definitions(uasync INTERFACE -DDISABLE_UCONTEXT=1)
endif()

if (USE_ZCONTEXT)
enable_language(ASM)
add_library(zcontext asm/zcontext_x86_64.S)
target_compile_definitions(uasync INTERFACE -DUSE_ZCONTEXT=1)
target_link_libraries(uasync INTERFACE zcontext)
endif()

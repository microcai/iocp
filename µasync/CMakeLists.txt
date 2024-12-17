

option(DISABLE_BOOST_CONTEXT "use ucontext even if boost is found" OFF)
option(DISABLE_UCONTEXT "disable ucontext and use setjmp/longjmp" OFF)

cmake_policy(SET CMP0167 NEW)
find_package(Boost 1.86 COMPONENTS context)


add_library(uasync INTERFACE)

target_include_directories(uasync INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NOT WIN32)
target_link_libraries(uasync INTERFACE iocp4linux)
endif()

set_property(TARGET uasync PROPERTY CXX_STANDARD 23)

if (BOOST_FOUND AND NOT DISABLE_BOOST_CONTEXT)
target_compile_definitions(uasync INTERFACE -DUSE_BOOST_CONTEXT=1)
target_link_libraries(uasync INTERFACE Boost::context)
endif()

if (DISABLE_UCONTEXT)
target_compile_definitions(uasync INTERFACE -DDISABLE_UCONTEXT=1)
endif()
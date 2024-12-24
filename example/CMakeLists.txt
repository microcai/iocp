cmake_minimum_required(VERSION 3.25)
project(iocp_tests)

link_libraries(uasync)

if (MSVC)
    add_link_options(/NODEFAULTLIB:libucrt$<$<CONFIG:Debug>:d>.lib)
    link_libraries(ucrt$<$<CONFIG:Debug>:d>.lib)
endif(MSVC)

if (WIN32)
link_libraries(ws2_32.lib kernel32.lib ntdll.lib)
endif()

add_executable(echo_server_callback echo_server/echo_server_callback.cpp)
add_executable(echo_server_stackfull echo_server/echo_server_stackfull.cpp)
add_executable(echo_server_stackless echo_server/echo_server_stackless.cpp)

add_executable(web_server web_server/server.cpp)

add_executable(example_on_zhihu zhihu.cpp)

add_executable(echo_client_stackfull echo_client/echo_client_stackfull.cpp)

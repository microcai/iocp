cmake_minimum_required(VERSION 3.25)
project(iocp_tests)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

link_libraries(uasync)

if (MSVC)
    add_link_options(/NODEFAULTLIB:libucrt$<$<CONFIG:Debug>:d>.lib)
    link_libraries(ucrt$<$<CONFIG:Debug>:d>.lib)
endif(MSVC)

if (WIN32)
link_libraries(ws2_32.lib kernel32.lib ntdll.lib)
endif()

add_executable(echo_server_callback echo_server/echo_server_callback.cpp)
set_target_properties(echo_server_callback PROPERTIES FOLDER "examples")
add_executable(echo_server_stackfull echo_server/echo_server_stackfull.cpp)
set_target_properties(echo_server_stackfull PROPERTIES FOLDER "examples")
add_executable(echo_server_stackless echo_server/echo_server_stackless.cpp)
set_target_properties(echo_server_stackless PROPERTIES FOLDER "examples")

add_executable(web_server web_server/server.cpp)
set_target_properties(web_server PROPERTIES FOLDER "examples")

add_executable(example_on_zhihu zhihu.cpp)
set_target_properties(example_on_zhihu PROPERTIES FOLDER "examples")

add_executable(echo_client_stackfull echo_client/echo_client_stackfull.cpp)
set_target_properties(echo_client_stackfull PROPERTIES FOLDER "examples")

add_subdirectory(unbufcpy)

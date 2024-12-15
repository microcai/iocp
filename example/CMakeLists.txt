cmake_minimum_required(VERSION 3.25)
project(iocp_tests)

link_libraries(uasync)

find_package(Boost 1.86 COMPONENTS context)

add_executable(echo_server_callback echo_server/echo_server_callback.cpp)
add_executable(echo_server_stackfull echo_server/echo_server_stackfull.c)
add_executable(echo_server_stackless echo_server/echo_server_stackless.cpp)

add_executable(web_server web_server/server.cpp)

add_executable(example_on_zhihu zhihu.cpp)

add_executable(echo_client_stackfull echo_client/echo_client_stackfull.cpp)
if (BOOST_FOUND)
target_compile_definitions(echo_client_stackfull PRIVATE -DUSE_BOOST_CONTEXT=1)
target_link_libraries(echo_client_stackfull Boost::context)
endif()
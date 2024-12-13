cmake_minimum_required(VERSION 3.25)
project(iocp_tests)

link_libraries(uasync)

add_executable(iocp_test test.cpp)

add_executable(echo_server echo_server/echo_server.cpp)

add_executable(web_server web_server/server.cpp)

add_executable(example_on_zhihu zhihu.cpp)

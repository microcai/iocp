cmake_minimum_required(VERSION 3.25)
project(iocp_tests)

add_executable(iocp_test test.cpp)
target_link_libraries(iocp_test iocp)


add_executable(echo_server echo_server/echo_server.cpp)
target_link_libraries(echo_server iocp)


add_executable(web_server web_server/server.cpp)
target_link_libraries(web_server iocp)

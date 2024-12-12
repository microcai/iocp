
add_library(iocp4linux src/iocp.cpp include/iocp.h src/operation_allocator.hpp)
target_include_directories(iocp4linux INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_include_directories(iocp4linux PRIVATE include/ asio/asio/include)

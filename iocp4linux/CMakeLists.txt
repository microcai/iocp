
add_library(iocp4linux src/iocp.cpp include/iocp.h src/io_uring_operation_allocator.hpp src/wsa.cpp)
target_include_directories(iocp4linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

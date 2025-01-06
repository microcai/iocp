
add_library(iocp4linux src/iocp.cpp include/iocp.h src/io_uring_operation_allocator.hpp src/wsa.cpp)
target_include_directories(iocp4linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(iocp4linux PRIVATE -ffunction-sections)
endif()

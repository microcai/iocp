
add_library(uasync INTERFACE)

target_include_directories(uasync INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NOT WIN32)
target_link_libraries(uasync INTERFACE iocp4linux)
endif()

set_property(TARGET uasync PROPERTY CXX_STANDARD 23)

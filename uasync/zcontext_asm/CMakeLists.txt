
if(MSVC)
    if (CMAKE_GENERATOR MATCHES "Ninja")
        set(CMAKE_ASM_MASM_CREATE_STATIC_LIBRARY "<CMAKE_AR> /OUT:<TARGET> <LINK_FLAGS> <OBJECTS>")
    endif()

    enable_language(ASM_MASM)
    add_library(zcontext zcontext_x86_64.asm)
else()
    enable_language(ASM)

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        add_library(zcontext zcontext_arm64.S)
    else()
        add_library(zcontext zcontext_x86_64.S)
    endif()
endif()

target_include_directories(zcontext INTERFACE .)

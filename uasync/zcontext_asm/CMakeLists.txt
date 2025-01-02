if(MSVC)
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

cmake_minimum_required(VERSION 3.24)
project(srport C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Port of SR.EXE, VGA 16-colour mode (0Dh) with the PC speaker. CGA / EGA / Tandy / Hercules are
# parked (PLAN.md, TODO).

find_package(SDL3 REQUIRED CONFIG)

file(GLOB_RECURSE SR_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(srport ${SR_SOURCES})
target_include_directories(srport PRIVATE src)
target_link_libraries(srport PRIVATE SDL3::SDL3)
if(MSVC)
    target_compile_options(srport PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(srport PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(srport PRIVATE -Wno-format-truncation)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET srport POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:srport> $<TARGET_FILE_DIR:srport>
        COMMAND_EXPAND_LISTS)
endif()

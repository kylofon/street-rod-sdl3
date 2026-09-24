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

# Developer check of the sound module (tools/snd_dump.c; port/spec/sound.md, PORTING.md "Sound"):
# renders songs / effects offline through the host speaker model and logs the sequencer events.
file(GLOB SR_SOUND_SOURCES CONFIGURE_DEPENDS src/sound/*.c)
add_executable(srsnd-dump tools/snd_dump.c src/host.c src/mem.c ${SR_SOUND_SOURCES})
target_include_directories(srsnd-dump PRIVATE src)
target_link_libraries(srsnd-dump PRIVATE SDL3::SDL3)
if(NOT MSVC)
    target_compile_options(srsnd-dump PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
endif()

# shell/cmake/esh.cmake
# Shared CMake build logic for Google Embedded Shell examples.
# Include this from per-example CMakeLists.txt after setting project variables.
# NOTE: CMakeLists.txt must call cmake_minimum_required(), set toolchain
# variables (CMAKE_SYSTEM_NAME, CMAKE_C_COMPILER, etc.), and call project()
# BEFORE including this file.
#
# Required variables (set before including this file):
#   TOOLCHAIN_PREFIX  - e.g. "aarch64-none-linux-gnu-"
#   STARTUP           - e.g. "aarch64" or "aarch64-multicore"
#   RAM_BASE_PHYSICAL - e.g. "0x40000000"
#   RAM_SIZE          - e.g. "0x4000"
#
# Optional variables:
#   PROJECT           - output name (default: "shell")
#   OPTIMIZATION      - optimization level (default: "0")
#   ROM_BASE_PHYSICAL - ROM base address (enables two-seg linker)
#   ROM_SIZE          - ROM size (required if ROM_BASE_PHYSICAL is set)
#   USER_LAYOUT_FILE  - custom linker script path (overrides all)
#   LD_FLAGS          - additional linker flags (list)
#   ASM_FLAGS         - additional assembler flags (list)
#   DEFINES           - additional preprocessor defines (list)
#   EXTERN_SRC        - additional source directories (list)
#   IGNORE_SRC_PATH   - paths to exclude from source discovery (list)
#   SHELL_LITE        - set to 1 to exclude features (default: 0)
#   ECHO_INIT_VALUE   - default: "0x1"
#   PROMPT            - default: "#"

# ==============================================================================
# Phase 1: Validation
# ==============================================================================

if(NOT TOOLCHAIN_PREFIX)
    message(FATAL_ERROR "TOOLCHAIN_PREFIX not set! Example: set(TOOLCHAIN_PREFIX \"aarch64-none-linux-gnu-\")")
endif()
if(NOT STARTUP)
    message(FATAL_ERROR "STARTUP not set! Example: set(STARTUP \"aarch64\")")
endif()
if(NOT USER_LAYOUT_FILE)
    if(NOT RAM_BASE_PHYSICAL)
        message(FATAL_ERROR "RAM_BASE_PHYSICAL not set!")
    endif()
    if(NOT RAM_SIZE)
        message(FATAL_ERROR "RAM_SIZE not set!")
    endif()
endif()

# ==============================================================================
# Phase 2: Defaults
# ==============================================================================

if(NOT PROJECT)
    set(PROJECT "shell")
endif()
if(NOT OPTIMIZATION)
    set(OPTIMIZATION "0")
endif()
if(NOT ECHO_INIT_VALUE)
    set(ECHO_INIT_VALUE "0x1")
endif()
if(NOT PROMPT)
    set(PROMPT "#")
endif()
if(NOT DEFINED SHELL_LITE)
    set(SHELL_LITE 0)
endif()

# ==============================================================================
# Phase 3: Locate SHELL_ROOT
# ==============================================================================
# Directory structure: google_esh/shell/   google_esh/examples/emulation/<example>/
# From the example dir, go up 3 levels to reach google_esh root.

get_filename_component(_EXAMPLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(_CATEGORY_DIR "${_EXAMPLE_DIR}" DIRECTORY)
get_filename_component(_EXAMPLES_DIR "${_CATEGORY_DIR}" DIRECTORY)
get_filename_component(_ESH_ROOT_DIR "${_EXAMPLES_DIR}" DIRECTORY)
set(SHELL_ROOT "${_ESH_ROOT_DIR}/shell")

if(NOT EXISTS "${SHELL_ROOT}/Makefile")
    message(FATAL_ERROR "Cannot find shell/ at ${SHELL_ROOT}")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# ==============================================================================
# Phase 4: Build info
# ==============================================================================

execute_process(COMMAND whoami   OUTPUT_VARIABLE BUILD_USER OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND hostname OUTPUT_VARIABLE BUILD_HOST OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SHELL_ROOT}"
    OUTPUT_VARIABLE SHELL_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE USER_REPO_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# ==============================================================================
# Phase 5: Source discovery
# ==============================================================================
# Rules (mirroring find-source.mk):
#   - C/C++ sources: find recursively in project dir, shell dir, EXTERN_SRC
#   - Assembly (.S):  find recursively in project dir ONLY (not shell/)
#   - Startup file:   find by exact name in project dir (first), then shell dir
#   - Startup is removed from the ASM list to avoid double-compilation

file(GLOB_RECURSE PROJECT_C_SRCS   "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
file(GLOB_RECURSE PROJECT_CPP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE SHELL_C_SRCS     "${SHELL_ROOT}/*.c")
file(GLOB_RECURSE SHELL_CPP_SRCS   "${SHELL_ROOT}/*.cpp")

set(ALL_EXTERN_C_SRCS "")
set(ALL_EXTERN_CPP_SRCS "")
foreach(_ext_dir ${EXTERN_SRC})
    file(GLOB_RECURSE _ext_c   "${_ext_dir}" "*.c")
    file(GLOB_RECURSE _ext_cpp "${_ext_dir}" "*.cpp")
    list(APPEND ALL_EXTERN_C_SRCS   ${_ext_c})
    list(APPEND ALL_EXTERN_CPP_SRCS ${_ext_cpp})
endforeach()

# Assembly sources: ONLY from project directory (NOT shell/)
file(GLOB_RECURSE PROJECT_ASM_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.S")

# Apply IGNORE_SRC_PATH filtering
foreach(_ignore_path ${IGNORE_SRC_PATH})
    file(GLOB_RECURSE _ignore_c   "${_ignore_path}" "*.c")
    file(GLOB_RECURSE _ignore_cpp "${_ignore_path}" "*.cpp")
    list(REMOVE_ITEM PROJECT_C_SRCS   ${_ignore_c})
    list(REMOVE_ITEM PROJECT_CPP_SRCS ${_ignore_cpp})
    list(REMOVE_ITEM SHELL_C_SRCS     ${_ignore_c})
    list(REMOVE_ITEM SHELL_CPP_SRCS   ${_ignore_cpp})
endforeach()

set(ALL_C_SRCS   ${PROJECT_C_SRCS}   ${SHELL_C_SRCS}   ${ALL_EXTERN_C_SRCS})
set(ALL_CPP_SRCS ${PROJECT_CPP_SRCS} ${SHELL_CPP_SRCS} ${ALL_EXTERN_CPP_SRCS})

# Startup file: project dir first, then shell dir (recursive search)
# Use find command (same as Makefile) to locate startup by name
execute_process(
    COMMAND find "${CMAKE_CURRENT_SOURCE_DIR}" -maxdepth 5 -type f -name "${STARTUP}.S"
    OUTPUT_VARIABLE _PROJ_STARTUP_CANDIDATES_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT _PROJ_STARTUP_CANDIDATES_RAW)
    execute_process(
        COMMAND find "${CMAKE_CURRENT_SOURCE_DIR}" -maxdepth 5 -type f -name "${STARTUP}"
        OUTPUT_VARIABLE _PROJ_STARTUP_CANDIDATES_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

string(REPLACE "\n" ";" _PROJ_STARTUP_LIST "${_PROJ_STARTUP_CANDIDATES_RAW}")
set(_PROJ_STARTUP_CANDIDATES "")
foreach(_f ${_PROJ_STARTUP_LIST})
    string(FIND "${_f}" "/objects/" _pos1)
    string(FIND "${_f}" "/build/" _pos2)
    if(_pos1 LESS 0 AND _pos2 LESS 0)
        list(APPEND _PROJ_STARTUP_CANDIDATES "${_f}")
    endif()
endforeach()

if(_PROJ_STARTUP_CANDIDATES)
    list(GET _PROJ_STARTUP_CANDIDATES 0 STARTUP_FILE)
else()
    execute_process(
        COMMAND find "${SHELL_ROOT}" -type f -name "${STARTUP}.S"
        OUTPUT_VARIABLE _SHELL_STARTUP_CANDIDATES_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _SHELL_STARTUP_CANDIDATES_RAW)
        execute_process(
            COMMAND find "${SHELL_ROOT}" -type f -name "${STARTUP}"
            OUTPUT_VARIABLE _SHELL_STARTUP_CANDIDATES_RAW
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    string(REPLACE "\n" ";" _SHELL_STARTUP_LIST "${_SHELL_STARTUP_CANDIDATES_RAW}")
    set(_SHELL_STARTUP_CANDIDATES "")
    foreach(_f ${_SHELL_STARTUP_LIST})
        string(FIND "${_f}" "/objects/" _pos1)
        string(FIND "${_f}" "/build/" _pos2)
        if(_pos1 LESS 0 AND _pos2 LESS 0)
            list(APPEND _SHELL_STARTUP_CANDIDATES "${_f}")
        endif()
    endforeach()
    if(_SHELL_STARTUP_CANDIDATES)
        list(GET _SHELL_STARTUP_CANDIDATES 0 STARTUP_FILE)
    endif()
endif()

if(NOT STARTUP_FILE)
    message(FATAL_ERROR "Startup file '${STARTUP}' not found in project or shell directory")
endif()

# Remove startup from assembly sources to avoid double-compilation
list(REMOVE_ITEM PROJECT_ASM_SRCS "${STARTUP_FILE}")

# ==============================================================================
# Phase 6: Include paths (mirrors find-headers.mk)
# ==============================================================================

file(GLOB_RECURSE _all_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
file(GLOB_RECURSE _shell_headers
    "${SHELL_ROOT}/*.h"
    "${SHELL_ROOT}/*.hpp"
)
list(APPEND _all_headers ${_shell_headers})
foreach(_ext_dir ${EXTERN_SRC})
    file(GLOB_RECURSE _ext_h   "${_ext_dir}" "*.h")
    file(GLOB_RECURSE _ext_hpp "${_ext_dir}" "*.hpp")
    list(APPEND _all_headers ${_ext_h} ${_ext_hpp})
endforeach()

set(_INCLUDE_DIRS "")
foreach(_hdr ${_all_headers})
    get_filename_component(_hdr_dir "${_hdr}" DIRECTORY)
    list(APPEND _INCLUDE_DIRS "${_hdr_dir}")
endforeach()
list(REMOVE_DUPLICATES _INCLUDE_DIRS)

# ==============================================================================
# Phase 7: Compiler defines and flags
# ==============================================================================
# In the Makefile, DEFINES holds raw compiler flags passed directly to gcc.
# These may be preprocessor defines (-DSYMBOL=val) or compiler flags (-mcpu=...).
# Mirror this by passing DEFINES as-is via target_compile_options.

set(_ESH_DEFINES
    "-DRAM_BASE_PHYSICAL=${RAM_BASE_PHYSICAL}"
    "-DECHO_INIT_VALUE=${ECHO_INIT_VALUE}"
    "-D__BUILD_USER__=${BUILD_USER}"
    "-D__BUILD_HOST__=${BUILD_HOST}"
    "-D__SHELL_VERSION__=${SHELL_VERSION}"
    "-D__USER_REPO_VERSION__=${USER_REPO_VERSION}"
)

if(SHELL_LITE)
    list(APPEND _ESH_DEFINES
        "-DSHELL_PRINTF_LITE"
        "-DSHELL_NO_PRINTF_LL"
        "-DSHELL_NO_UTILS"
        "-DSHELL_NO_HISTORY"
        "-DSHELL_NO_TAB_COMPLETE"
        "-DSHELL_NO_BIT_UTILS"
    )
endif()

# ==============================================================================
# Phase 8: Linker script selection
# ==============================================================================

set(LD_MACROS "")

if(USER_LAYOUT_FILE)
    set(LD_FILE "${USER_LAYOUT_FILE}")
else()
    if(ROM_BASE_PHYSICAL)
        set(LD_FILE "${SHELL_ROOT}/scatter/two-seg.ld")
        list(APPEND LD_MACROS
            "--defsym=__ROM_BASE__=${ROM_BASE_PHYSICAL}"
            "--defsym=__ROM_SIZE__=${ROM_SIZE}"
        )
    else()
        set(LD_FILE "${SHELL_ROOT}/scatter/one-seg.ld")
    endif()
    list(APPEND LD_MACROS
        "--defsym=__RAM_BASE__=${RAM_BASE_PHYSICAL}"
        "--defsym=__RAM_SIZE__=${RAM_SIZE}"
    )
endif()

# ==============================================================================
# Phase 9: Compile and link
# ==============================================================================

set(ALL_SOURCES
    ${ALL_C_SRCS}
    ${ALL_CPP_SRCS}
    ${PROJECT_ASM_SRCS}
    ${STARTUP_FILE}
)

add_library(esh_objects OBJECT ${ALL_SOURCES})

target_include_directories(esh_objects PRIVATE ${_INCLUDE_DIRS})

# PROMPT contains '#' which CMake define handling cannot process correctly.
# Pass it directly as a raw compiler flag.
string(CONCAT _PROMPT_FLAG "-D__PROMPT__=" "${PROMPT}")

target_compile_options(esh_objects PRIVATE
    ${DEFINES}
    ${_ESH_DEFINES}
    "${_PROMPT_FLAG}"
    -Wall
    -O${OPTIMIZATION}
    -nostdlib
    -nostartfiles
    -ffreestanding
    -ggdb
)

# C++ extra flag
set_source_files_properties(${ALL_CPP_SRCS} PROPERTIES
    COMPILE_FLAGS "-Wwrite-strings"
)
# ASM extra flags
if(ASM_FLAGS)
    set_source_files_properties(${PROJECT_ASM_SRCS} PROPERTIES
        COMPILE_FLAGS "${ASM_FLAGS}"
    )
    set_source_files_properties(${STARTUP_FILE} PROPERTIES
        COMPILE_FLAGS "${ASM_FLAGS}"
    )
endif()

# --- Custom link step using ld directly ---
set(OBJDUMP "${TOOLCHAIN_PREFIX}objdump")
set(OBJCOPY "${TOOLCHAIN_PREFIX}objcopy")

set(OUTPUT_ELF "${PROJECT}.elf")
set(OUTPUT_MAP "${PROJECT}.elf.map")
set(OUTPUT_LST "${PROJECT}.elf.lst")
set(OUTPUT_BIN "${PROJECT}.bin")

add_custom_command(
    OUTPUT "${OUTPUT_ELF}" "${OUTPUT_MAP}" "${OUTPUT_LST}" "${OUTPUT_BIN}"
    COMMAND_EXPAND_LISTS
    COMMAND ${CMAKE_LINKER}
        -T "${LD_FILE}"
        $<TARGET_OBJECTS:esh_objects>
        ${LD_FLAGS}
        --print-memory-usage
        ${LD_MACROS}
        -o "${OUTPUT_ELF}"
        -Map="${OUTPUT_MAP}"
    COMMAND ${OBJDUMP} -D -S "${OUTPUT_ELF}" > "${OUTPUT_LST}"
    COMMAND ${OBJCOPY} -O binary "${OUTPUT_ELF}" "${OUTPUT_BIN}"
    DEPENDS esh_objects "${LD_FILE}"
    COMMENT "Linking ${OUTPUT_ELF}"
    VERBATIM
)

add_custom_target(${PROJECT} ALL
    DEPENDS "${OUTPUT_ELF}" "${OUTPUT_MAP}" "${OUTPUT_LST}" "${OUTPUT_BIN}"
)

# ==============================================================================
# Phase 10: Build configuration report
# ==============================================================================

message(STATUS "=== esh Build Configuration ===")
message(STATUS "PROJECT           : ${PROJECT}")
message(STATUS "TOOLCHAIN         : ${TOOLCHAIN_PREFIX}")
message(STATUS "STARTUP           : ${STARTUP_FILE}")
message(STATUS "LAYOUT FILE       : ${LD_FILE}")
message(STATUS "RAM_BASE          : ${RAM_BASE_PHYSICAL}")
message(STATUS "RAM_SIZE          : ${RAM_SIZE}")
message(STATUS "ROM_BASE          : ${ROM_BASE_PHYSICAL}")
message(STATUS "ROM_SIZE          : ${ROM_SIZE}")
message(STATUS "OPTIMIZATION      : ${OPTIMIZATION}")
message(STATUS "BUILD_USER        : ${BUILD_USER}")
message(STATUS "BUILD_HOST        : ${BUILD_HOST}")
message(STATUS "SHELL_VERSION     : ${SHELL_VERSION}")
message(STATUS "USER_REPO_VERSION : ${USER_REPO_VERSION}")

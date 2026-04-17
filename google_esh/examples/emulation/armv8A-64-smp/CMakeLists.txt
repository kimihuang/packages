cmake_minimum_required(VERSION 3.16)

set(TOOLCHAIN_PREFIX  "aarch64-none-linux-gnu-")
set(OPTIMIZATION      "g")
set(STARTUP           "aarch64-multicore")
set(RAM_BASE_PHYSICAL "0x40100000")
set(RAM_SIZE          "0x40000")
set(LD_FLAGS          "--no-warn-rwx-segments")

# Toolchain must be set before project() call
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}ld")

project(shell LANGUAGES C ASM)

include(../../../shell/cmake/esh.cmake)

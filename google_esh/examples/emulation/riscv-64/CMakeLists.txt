cmake_minimum_required(VERSION 3.16)

set(TOOLCHAIN_PREFIX  "riscv64-linux-gnu-")
set(OPTIMIZATION      "0")
set(STARTUP           "riscv64")
set(RAM_BASE_PHYSICAL "0x80000000")
set(RAM_SIZE          "0x4000000")
set(LD_FLAGS          "--no-warn-rwx-segments")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}ld")

project(shell LANGUAGES C ASM)

include(../../../shell/cmake/esh.cmake)

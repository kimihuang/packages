cmake_minimum_required(VERSION 3.16)

set(TOOLCHAIN_PREFIX  "riscv64-linux-gnu-")
set(OPTIMIZATION      "g")
set(STARTUP           "riscv32")
set(ROM_BASE_PHYSICAL "0x20010000")
set(ROM_SIZE          "0x00010000")
set(RAM_BASE_PHYSICAL "0x80000000")
set(RAM_SIZE          "0x00004000")
set(DEFINES           "-march=rv32imac;-mabi=ilp32")
set(ASM_FLAGS         "-march=rv32imac;-mabi=ilp32")
set(LD_FLAGS          "-m;elf32lriscv")
set(SHELL_LITE        1)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}ld")

project(shell LANGUAGES C ASM)

include(../../../shell/cmake/esh.cmake)

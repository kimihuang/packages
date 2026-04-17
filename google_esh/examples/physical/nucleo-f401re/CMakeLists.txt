cmake_minimum_required(VERSION 3.16)

set(TOOLCHAIN_PREFIX  "arm-none-eabi-")
set(OPTIMIZATION      "g")
set(STARTUP           "cortex-m")
set(ROM_BASE_PHYSICAL "0x8000000")
set(ROM_SIZE          "0x0004000")
set(RAM_BASE_PHYSICAL "0x20000000")
set(RAM_SIZE          "0x00008000")
set(DEFINES           "-mcpu=cortex-m4;-mthumb;-mtune=cortex-m4")
set(ASM_FLAGS         "-march=armv7e-m")
set(LD_FLAGS          "-lgcc;-L/usr/lib/gcc/arm-none-eabi")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}ld")

project(shell LANGUAGES C ASM)

include(../../../shell/cmake/esh.cmake)

cmake_minimum_required(VERSION 3.16)

set(TOOLCHAIN_PREFIX  "arm-none-eabi-")
set(OPTIMIZATION      "g")
set(STARTUP           "cortex-m33")
set(ROM_BASE_PHYSICAL "0x10000000")
set(ROM_SIZE          "0x0007FFFF")
set(RAM_BASE_PHYSICAL "0x30000000")
set(RAM_SIZE          "0x00007FFF")
set(DEFINES           "-mcpu=cortex-m33;-mthumb;-DSHELL_NO_PRINTF_LL;-DSTACK_START=0x30007FFF;-DSTACK_START2=0x3000FFFF")
set(ASM_FLAGS         "-march=armv8-m.main")
set(LD_FLAGS          "-lgcc;-L/usr/lib/gcc/arm-none-eabi")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}ld")

project(shell LANGUAGES C ASM)

include(../../../shell/cmake/esh.cmake)

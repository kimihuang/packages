cmake_minimum_required(VERSION 3.10)
project(crash_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compile options: -g for debug info, -O0 for best debugging experience, -fno-omit-frame-pointer
add_compile_options(-Wall -Wextra -g -O0 -fno-omit-frame-pointer)

add_executable(crash_test crash_test.c)

install(TARGETS crash_test DESTINATION bin)

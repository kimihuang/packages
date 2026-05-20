cmake_minimum_required(VERSION 3.10)
project(loop_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -g -O0 -fno-omit-frame-pointer)

add_executable(loop_test loop_test.c)

target_link_libraries(loop_test pthread)

install(TARGETS loop_test DESTINATION bin)

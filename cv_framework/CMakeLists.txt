cmake_minimum_required(VERSION 3.10)
project(cv_test_framework C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compile options
add_compile_options(-Wall -Wextra -g -fno-omit-frame-pointer)

# Framework header path
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---- Executable target ----
add_executable(cv_test
    src/cv_main.c
    src/cv_test.c
    src/cv_runner.c
    src/cv_option.c
    tests/test_math.c
    tests/test_string.c
    tests/test_i2c.c
    tests/test_timer.c
)

# ---- Custom targets ----
add_custom_target(run
    COMMAND $<TARGET_FILE:cv_test>
    DEPENDS cv_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(test
    COMMAND $<TARGET_FILE:cv_test>
    DEPENDS cv_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

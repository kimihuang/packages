cmake_minimum_required(VERSION 3.14)
project(sort_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 主程序源文件（不含 main.c，由可执行目标单独添加）
set(SORT_SOURCES
    src/sort_bubble.c
    src/sort_selection.c
    src/sort_insertion.c
    src/sort_shell.c
    src/sort_merge.c
    src/sort_quick.c
    src/sort_heap.c
    src/sort_registry.c
)

# 排序算法静态库，便于测试和主程序复用
add_library(sort_lib STATIC ${SORT_SOURCES})
target_include_directories(sort_lib PUBLIC include)

# 主程序
add_executable(sort_app src/main.c)
target_link_libraries(sort_app PRIVATE sort_lib)

# 测试
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

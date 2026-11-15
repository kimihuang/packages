cmake_minimum_required(VERSION 3.10)
project(notify_benchmark CXX)

# 使用 C++17（condition_variable、std::thread、std::atomic 需要 C++11 即可，
# 但为便于后续扩展统一使用 C++17）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Release 构建启用优化；调度延迟测量对优化敏感，务必使用 -O2 以上
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

add_executable(notify_benchmark
    main.cpp
    notify_benchmark.cpp
)

# pthread 链接（std::thread 依赖）
find_package(Threads REQUIRED)
target_link_libraries(notify_benchmark PRIVATE Threads::Threads)

# 编译告警
target_compile_options(notify_benchmark PRIVATE -Wall -Wextra)

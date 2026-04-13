cmake_minimum_required(VERSION 3.10)
project(elog C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
add_compile_options(-Wall -Wextra -Wno-unused-parameter -pthread)

# 头文件搜索路径
include_directories(${CMAKE_SOURCE_DIR}/inc)

# ===== 库 =====
set(ELOG_SOURCES
    src/elog.c
    src/elog_filter.c
    src/elog_format.c
    src/elog_stats.c
    src/elog_prune.c
    src/elog_event.c
    src/elog_buf_ring.c
    src/elog_transport_stdout.c
    src/elog_transport_file.c
    src/elog_port_linux.c
    src/elog_reader.c
)

add_library(elog STATIC ${ELOG_SOURCES})

# ===== elogd 守护进程 =====
add_executable(elogd
    daemon/elogd.c
    daemon/elogd_listener.c
    daemon/elogd_cmd.c
    daemon/elogd_flush.c
)
target_link_libraries(elogd PRIVATE elog)
target_include_directories(elogd PRIVATE ${CMAKE_SOURCE_DIR}/daemon)

# ===== 测试 =====
enable_testing()

set(TEST_SOURCES
    test/test_main.c
    test/test_elog.c
    test/test_elog_buf.c
    test/test_elog_format.c
    test/test_elog_stats.c
    test/test_elog_prune.c
    test/test_elog_event.c
    test/test_elog_transport.c
    test/test_elog_transport_file.c
    test/test_elog_port.c
    test/test_elog_reader.c
    test/test_elogd.c
)

add_executable(elog_test ${TEST_SOURCES})
target_link_libraries(elog_test PRIVATE elog)

add_test(NAME elog_test COMMAND elog_test)

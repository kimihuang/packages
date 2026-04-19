include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 排序正确性测试
add_executable(sort_tests
    test_sort.cpp
)
target_link_libraries(sort_tests PRIVATE sort_lib GTest::gtest_main)
add_test(NAME sort_tests COMMAND sort_tests)

# 性能基准测试
add_executable(sort_bench
    test_bench.cpp
)
target_link_libraries(sort_bench PRIVATE sort_lib GTest::gtest_main)
add_test(NAME sort_bench COMMAND sort_bench)

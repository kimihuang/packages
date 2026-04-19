#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <vector>

#include "sort/sort_registry.h"
#include "sort/sort_quick.h"

/* 性能基准测试 —— 只关注大数组排序耗时 */

static std::vector<int> make_random(size_t n) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 1000000);
    std::vector<int> v(n);
    for (auto &x : v) x = dist(rng);
    return v;
}

/* 快速排序 vs 其他 O(n log n) 算法在大数组下的性能对比 */
TEST(BenchTest, LargeArray_100K) {
    const size_t N = 100000;
    auto original = make_random(N);

    const sort_algorithm_t *algos[] = {
        sort_registry_find("merge"),
        sort_registry_find("quick"),
        sort_registry_find("heap"),
        sort_registry_find("shell"),
    };

    for (auto *algo : algos) {
        ASSERT_NE(algo, nullptr);
        auto data = original;
        auto start = std::chrono::high_resolution_clock::now();
        algo->func(data.data(), data.size());
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();

        ASSERT_TRUE(std::is_sorted(data.begin(), data.end()));
        printf("  %-12s  %8.2f ms\n", algo->name, ms);
    }
}

/* 快速排序稳定性测试：使用已排序输入不应退化 */
TEST(BenchTest, SortedInput_1M) {
    const size_t N = 1000000;
    std::vector<int> data(N);
    std::iota(data.begin(), data.end(), 0);

    auto start = std::chrono::high_resolution_clock::now();
    sort_quick(data.data(), data.size());
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    ASSERT_TRUE(std::is_sorted(data.begin(), data.end()));
    printf("  quick(sorted %zu): %.2f ms\n", N, ms);
}

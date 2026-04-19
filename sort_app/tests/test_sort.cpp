#include <gtest/gtest.h>
#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include "sort/sort_registry.h"

/* ---- 辅助工具 ---- */

static std::vector<int> make_random(size_t n, int lo = -10000, int hi = 10000) {
    std::mt19937 rng(42);  /* 固定种子，结果可复现 */
    std::uniform_int_distribution<int> dist(lo, hi);
    std::vector<int> v(n);
    for (auto &x : v) x = dist(rng);
    return v;
}

static std::vector<int> make_sorted(size_t n) {
    std::vector<int> v(n);
    std::iota(v.begin(), v.end(), 0);
    return v;
}

static std::vector<int> make_reversed(size_t n) {
    std::vector<int> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (int)(n - 1 - i);
    return v;
}

static std::vector<int> make_same(size_t n, int val = 7) {
    return std::vector<int>(n, val);
}

static void run_and_verify(sort_func_t fn, std::vector<int> data) {
    fn(data.data(), data.size());
    ASSERT_TRUE(std::is_sorted(data.begin(), data.end()))
        << "Sorting failed for size=" << data.size();
}

/* ---- 每个算法独立测试 ---- */

class SortTest : public ::testing::TestWithParam<const sort_algorithm_t *> {};

TEST_P(SortTest, EmptyArray) {
    std::vector<int> v;
    run_and_verify(GetParam()->func, v);
}

TEST_P(SortTest, SingleElement) {
    run_and_verify(GetParam()->func, {42});
}

TEST_P(SortTest, TwoElements) {
    run_and_verify(GetParam()->func, {2, 1});
    run_and_verify(GetParam()->func, {1, 2});
}

TEST_P(SortTest, SameElements) {
    run_and_verify(GetParam()->func, make_same(100));
}

TEST_P(SortTest, AlreadySorted) {
    run_and_verify(GetParam()->func, make_sorted(100));
}

TEST_P(SortTest, Reversed) {
    run_and_verify(GetParam()->func, make_reversed(100));
}

TEST_P(SortTest, RandomSmall) {
    run_and_verify(GetParam()->func, make_random(10));
}

TEST_P(SortTest, RandomMedium) {
    run_and_verify(GetParam()->func, make_random(1000));
}

TEST_P(SortTest, RandomLarge) {
    run_and_verify(GetParam()->func, make_random(10000));
}

TEST_P(SortTest, NegativeValues) {
    run_and_verify(GetParam()->func, make_random(100, -9999, 9999));
}

TEST_P(SortTest, Duplicates) {
    auto v = make_random(100, 0, 5);  /* 只有 0~5，大量重复 */
    run_and_verify(GetParam()->func, v);
}

INSTANTIATE_TEST_SUITE_P(AllAlgorithms, SortTest,
    ::testing::ValuesIn(
        []() {
            const sort_algorithm_t *all = sort_registry_get_all();
            size_t cnt = sort_registry_count();
            std::vector<const sort_algorithm_t *> result;
            result.reserve(cnt);
            for (size_t i = 0; i < cnt; i++)
                result.push_back(&all[i]);
            return result;
        }()
    ),
    [](const ::testing::TestParamInfo<SortTest::ParamType> &info) {
        return info.param->name;  /* 测试名使用算法名 */
    }
);

/* ---- Registry 查找测试 ---- */

TEST(RegistryTest, FindExisting) {
    const sort_algorithm_t *a = sort_registry_find("quick");
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(a->name, "quick");
}

TEST(RegistryTest, FindNonExisting) {
    EXPECT_EQ(sort_registry_find("nonexistent"), nullptr);
    EXPECT_EQ(sort_registry_find(nullptr), nullptr);
}

TEST(RegistryTest, CountPositive) {
    EXPECT_GT(sort_registry_count(), 0u);
}

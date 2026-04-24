/*
 * test_math.c - group_math: addition, subtraction, multiplication tests
 */
#include "cv_macros.h"
#include <stdio.h>
#include <limits.h>

TEST_GROUP(group_math);

/* ---- Group level hooks ---- */
static void math_pre(void)
{
    printf("  [GROUP PRE_TEST] group_math init resources...\n");
}

static void math_post(void)
{
    printf("  [GROUP POST_TEST] group_math release resources...\n");
}

GROUP_PRE_TEST(group_math, math_pre);
GROUP_POST_TEST(group_math, math_post);

/* ==================== module_add ==================== */
TEST_MODULE(group_math, module_add);

static int add_result;

static void add_pre(void)
{
    printf("    [MODULE PRE_TEST] init adder context\n");
    add_result = 0;
}

static void add_post(void)
{
    printf("    [MODULE POST_TEST] destroy adder context\n");
}

MODULE_PRE_TEST(module_add, add_pre);
MODULE_POST_TEST(module_add, add_post);

TEST_CASE(module_add, test_1_plus_1)
{
    add_result = 1 + 1;
    CV_ASSERT(add_result == 2);
}

TEST_CASE(module_add, test_neg_add)
{
    add_result = -1 + (-1);
    CV_ASSERT(add_result == -2);
}

static void case_buf_pre(void)
{
    printf("      [CASE PRE_TEST] setup operand buffer\n");
}

static void case_buf_post(void)
{
    printf("      [CASE POST_TEST] cleanup operand buffer\n");
}

TEST_CASE(module_add, test_zero_add)
{
    add_result = 0 + 0;
    CV_ASSERT_EQ(add_result, 0);
}

/* Case-level hooks must be set at file scope (uses constructor) */
CASE_PRE_TEST(test_zero_add_cvcase, case_buf_pre);
CASE_POST_TEST(test_zero_add_cvcase, case_buf_post);

TEST_CASE(module_add, test_large_add)
{
    add_result = INT_MAX - 1 + 1;
    CV_ASSERT_EQ(add_result, INT_MAX);
}

/* ==================== module_sub ==================== */
TEST_MODULE(group_math, module_sub);

static void sub_pre(void)
{
    printf("    [MODULE PRE_TEST] init subtractor context\n");
}

static void sub_post(void)
{
    printf("    [MODULE POST_TEST] destroy subtractor context\n");
}

MODULE_PRE_TEST(module_sub, sub_pre);
MODULE_POST_TEST(module_sub, sub_post);

TEST_CASE(module_sub, test_5_minus_3)
{
    CV_ASSERT_EQ(5 - 3, 2);
}

TEST_CASE(module_sub, test_neg_minus_neg)
{
    CV_ASSERT(-1 - (-1) == 0);
}

TEST_CASE(module_sub, test_zero_minus_zero)
{
    CV_ASSERT_EQ(0 - 0, 0);
}

/* ==================== module_mul ==================== */
TEST_MODULE(group_math, module_mul);

static void mul_pre(void)
{
    printf("    [MODULE PRE_TEST] init multiplier context\n");
}

static void mul_post(void)
{
    printf("    [MODULE POST_TEST] destroy multiplier context\n");
}

MODULE_PRE_TEST(module_mul, mul_pre);
MODULE_POST_TEST(module_mul, mul_post);

TEST_CASE(module_mul, test_2_times_3)
{
    CV_ASSERT_EQ(2 * 3, 6);
}

TEST_CASE(module_mul, test_neg_times_neg)
{
    CV_ASSERT_EQ(-2 * -3, 6);
}

/* Intentional failure for demonstration */
TEST_CASE(module_mul, test_overflow)
{
    long result = (long)INT_MAX + 1;
    CV_ASSERT_EQ(result, 0);
}

/*
 * test_string.c - group_string: concatenation, length, copy tests
 */
#include "cv_macros.h"
#include <stdio.h>
#include <string.h>

TEST_GROUP(group_string);

/* ---- Group level hooks ---- */
static void str_pre(void)
{
    printf("  [GROUP PRE_TEST] group_string init buffer pool...\n");
}

static void str_post(void)
{
    printf("  [GROUP POST_TEST] group_string release buffer pool...\n");
}

GROUP_PRE_TEST(group_string, str_pre);
GROUP_POST_TEST(group_string, str_post);

/* ==================== module_concat ==================== */
TEST_MODULE(group_string, module_concat);

static char concat_buf[256];

static void concat_pre(void)
{
    printf("    [MODULE PRE_TEST] init concat buffer\n");
    memset(concat_buf, 0, sizeof(concat_buf));
}

static void concat_post(void)
{
    printf("    [MODULE POST_TEST] destroy concat buffer\n");
}

MODULE_PRE_TEST(module_concat, concat_pre);
MODULE_POST_TEST(module_concat, concat_post);

TEST_CASE(module_concat, test_basic_concat)
{
    strcpy(concat_buf, "hello");
    strcat(concat_buf, " world");
    CV_ASSERT(strcmp(concat_buf, "hello world") == 0);
}

TEST_CASE(module_concat, test_empty_concat)
{
    strcpy(concat_buf, "");
    strcat(concat_buf, "abc");
    CV_ASSERT(strcmp(concat_buf, "abc") == 0);
}

TEST_CASE(module_concat, test_concat_null_term)
{
    strcpy(concat_buf, "test\0hidden");
    strcat(concat_buf, "_suffix");
    CV_ASSERT(strcmp(concat_buf, "test_suffix") == 0);
}

/* ==================== module_len ==================== */
TEST_MODULE(group_string, module_len);

TEST_CASE(module_len, test_ascii_len)
{
    CV_ASSERT_EQ((int)strlen("hello"), 5);
}

TEST_CASE(module_len, test_empty_len)
{
    CV_ASSERT_EQ((int)strlen(""), 0);
}

TEST_CASE(module_len, test_space_len)
{
    CV_ASSERT_EQ((int)strlen("a b c"), 5);
}

/* ==================== module_copy ==================== */
TEST_MODULE(group_string, module_copy);

static char copy_dst[256];
static char copy_src[] = "source data";

static void copy_pre(void)
{
    printf("    [MODULE PRE_TEST] init copy buffers\n");
    memset(copy_dst, 0, sizeof(copy_dst));
}

static void copy_post(void)
{
    printf("    [MODULE POST_TEST] destroy copy buffers\n");
}

MODULE_PRE_TEST(module_copy, copy_pre);
MODULE_POST_TEST(module_copy, copy_post);

TEST_CASE(module_copy, test_strcpy_basic)
{
    strcpy(copy_dst, copy_src);
    CV_ASSERT(strcmp(copy_dst, copy_src) == 0);
}

TEST_CASE(module_copy, test_strncpy_exact)
{
    strncpy(copy_dst, copy_src, 6);
    copy_dst[6] = '\0';
    CV_ASSERT(strcmp(copy_dst, "source") == 0);
}

TEST_CASE(module_copy, test_memcpy)
{
    memcpy(copy_dst, copy_src, strlen(copy_src) + 1);
    CV_ASSERT(strcmp(copy_dst, copy_src) == 0);
    CV_ASSERT_EQ((int)strlen(copy_dst), (int)strlen(copy_src));
}

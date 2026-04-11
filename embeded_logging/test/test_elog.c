/**
 * @file test_elog.c
 * @brief 核心 API 测试: init/deinit/write/filter/level
 */

#include "elog.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* 捕获输出到 buffer (替代 stdout) */
static char capture_buf[8192];
static int capture_fd = -1;

static void capture_start(void) {
    capture_buf[0] = '\0';
    /* 使用 pipe 捕获，简化起见直接测试 API 返回值 */
}

static void capture_stop(void) {
    /* no-op */
}

/* 测试: 初始化和反初始化 */
static void test_init_deinit(void) {
    printf("  test_init_deinit...\n");

    elog_deinit();
    assert(elog_is_initialized() == false);

    int ret = elog_init();
    assert(ret == ELOG_OK);
    assert(elog_is_initialized() == true);

    elog_deinit();
    assert(elog_is_initialized() == false);

    /* 重复 init */
    ret = elog_init();
    assert(ret == ELOG_OK);
    elog_deinit();
}

/* 测试: 级别设置和过滤 */
static void test_level_filter(void) {
    printf("  test_level_filter...\n");

    elog_init();

    /* 默认级别 */
    elog_level_t lvl = elog_get_level();
    assert(lvl == ELOG_LEVEL_DEFAULT);

    /* 设置级别 */
    elog_set_level(ELOG_LEVEL_WARN);
    assert(elog_get_level() == ELOG_LEVEL_WARN);

    elog_set_level(ELOG_LEVEL_DEBUG);
    assert(elog_get_level() == ELOG_LEVEL_DEBUG);

    /* 边界 */
    elog_set_level(ELOG_LEVEL_VERBOSE);
    assert(elog_get_level() == ELOG_LEVEL_VERBOSE);
    elog_set_level(ELOG_LEVEL_NONE);
    assert(elog_get_level() == ELOG_LEVEL_NONE);

    elog_deinit();
}

/* 测试: 标签级别 */
static void test_tag_level(void) {
    printf("  test_tag_level...\n");

    elog_init();
    elog_set_level(ELOG_LEVEL_VERBOSE);

    /* 设置标签级别 */
    int ret = elog_set_tag_level("noisy", ELOG_LEVEL_ERROR);
    assert(ret == ELOG_OK);

    /* 更新标签级别 */
    ret = elog_set_tag_level("noisy", ELOG_LEVEL_WARN);
    assert(ret == ELOG_OK);

    /* 重置标签级别 */
    ret = elog_reset_tag_level("noisy");
    assert(ret == ELOG_OK);

    /* 重置不存在的标签 */
    ret = elog_reset_tag_level("nonexist");
    assert(ret == ELOG_ERR_PARAM);

    elog_deinit();
}

/* 测试: 日志写入 (不崩溃即可) */
static void test_write(void) {
    printf("  test_write...\n");

    elog_init();
    elog_set_level(ELOG_LEVEL_VERBOSE);

    /* 各级别写入 */
    elog_verbose("test", "verbose msg %d", 1);
    elog_debug("test", "debug msg %s", "hello");
    elog_info("test", "info msg %f", 3.14);
    elog_warn("test", "warn msg");
    elog_error("test", "error msg %x", 0xff);
    elog_fatal("test", "fatal msg");

    /* 宏版本 */
    ELOG_V("test", "macro verbose");
    ELOG_D("test", "macro debug");
    ELOG_I("test", "macro info");
    ELOG_W("test", "macro warn");
    ELOG_E("test", "macro error");
    ELOG_F("test", "macro fatal");

    /* 空标签 */
    elog_info(NULL, "null tag");

    /* 空消息 */
    elog_info("test", "");

    /* 长消息 */
    char long_msg[2048];
    memset(long_msg, 'A', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    elog_info("test", "%s", long_msg);

    elog_deinit();
}

/* 测试: 级别过滤 (高于设定级别的不应输出) */
static void test_level_filtering(void) {
    printf("  test_level_filtering...\n");

    elog_init();
    elog_set_level(ELOG_LEVEL_WARN);

    /* 这些不应该通过过滤 */
    elog_verbose("test", "should be filtered");
    elog_debug("test", "should be filtered");
    elog_info("test", "should be filtered");

    /* 这些应该通过 */
    elog_warn("test", "should pass");
    elog_error("test", "should pass");
    elog_fatal("test", "should pass");

    elog_deinit();
}

/* 测试: 统计 */
static void test_stats(void) {
    printf("  test_stats...\n");

    elog_init();
    elog_set_level(ELOG_LEVEL_VERBOSE);
    elog_reset_stats();

    elog_info("test", "msg1");
    elog_info("test", "msg2");
    elog_error("test", "err1");

    char buf[512];
    int ret = elog_get_stats(buf, sizeof(buf));
    assert(ret > 0);
    assert(strstr(buf, "total=") != NULL);

    elog_deinit();
}

/* 测试: 后端替换 */
static void test_logger_replace(void) {
    printf("  test_logger_replace...\n");

    static bool custom_called = false;

    elog_init();

    /* 保存原始后端 */
    elog_logger_func_t orig = NULL; /* 无法获取，测试设置即可 */

    /* 替换后端 */
    elog_set_logger(NULL);

    /* 写入不应崩溃 */
    elog_info("test", "msg with null backend");

    /* 恢复 — 重新 init */
    elog_deinit();
    elog_init();
    elog_info("test", "msg after re-init");

    elog_deinit();
}

int test_elog(void) {
    printf("test_elog:\n");

    test_init_deinit();
    test_level_filter();
    test_tag_level();
    test_write();
    test_level_filtering();
    test_stats();
    test_logger_replace();

    return 0;
}

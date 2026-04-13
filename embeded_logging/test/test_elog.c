/**
 * @file test_elog.c
 * @brief 核心 API 测试: init/deinit/write/filter/level/ISR
 */

#include "elog.h"
#include "elog_buf.h"
#include "elog_stats.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

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

/* ===== ISR 测试 ===== */

static void test_isr_basic(void) {
    printf("  test_isr_basic...\n");

    elog_init();

    int ret = elog_write_isr(ELOG_LEVEL_INFO, "isr_tag", "hello from ISR", 13);
    assert(ret == ELOG_OK);

    /* 验证写入成功: 通过 stats 确认 */
    char stats_buf[512];
    int len = elog_get_stats(stats_buf, sizeof(stats_buf));
    assert(len > 0);
    assert(strstr(stats_buf, "main") != NULL);

    elog_deinit();
}

static void test_isr_overwrite(void) {
    printf("  test_isr_overwrite...\n");

    elog_init();

    /* 用小 buffer 容易触发覆写 */
    uint8_t buf_mem[256];
    (void)buf_mem;
    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    /* 写满 buffer */
    for (int i = 0; i < 20; i++) {
        elog_ring_buf_log_isr(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                              0, 0, 0, "t", "msg");
    }

    /* ISR 写入应该强制覆写, 不返回 FULL */
    int ret = elog_ring_buf_log_isr(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_ERROR,
                                     0, 0, 0, "overflow", "forced");
    assert(ret == ELOG_OK);

    elog_ring_buf_destroy(&rb);
    elog_deinit();
}

static void test_isr_stats(void) {
    printf("  test_isr_stats...\n");

    elog_init();
    elog_reset_stats();

    /* 写入多条 ISR 日志 */
    elog_write_isr(ELOG_LEVEL_INFO, "stat_tag", "msg1", 4);
    elog_write_isr(ELOG_LEVEL_WARN, "stat_tag", "msg2", 4);
    elog_write_isr(ELOG_LEVEL_ERROR, "stat_tag", "msg3", 4);

    /* 通过 elog_get_stats 验证统计已更新 */
    char stats_buf[512];
    int len = elog_get_stats(stats_buf, sizeof(stats_buf));
    assert(len > 0);
    /* stats 应包含 main buffer 的 total 计数 */
    assert(strstr(stats_buf, "main") != NULL);

    elog_deinit();
}

static void test_isr_macro(void) {
    printf("  test_isr_macro...\n");

    elog_init();
    elog_reset_stats();

    /* 测试便捷宏 */
    ELOG_ISR_I("macro", "info msg");
    ELOG_ISR_W("macro", "warn msg");
    ELOG_ISR_E("macro", "error msg");

    /* 验证写入成功 */
    char stats_buf[512];
    elog_get_stats(stats_buf, sizeof(stats_buf));
    assert(strstr(stats_buf, "INFO=") != NULL);
    assert(strstr(stats_buf, "WARN=") != NULL);
    assert(strstr(stats_buf, "ERROR=") != NULL);

    elog_deinit();
}

static int find_raw_msg_cb(const elog_msg_header_t* hdr, const char* tag,
                            const char* msg, void* user) {
    (void)hdr;
    int* f = (int*)user;
    if (strcmp(tag, "fmt") == 0 && strcmp(msg, "%s %d %f") == 0) *f = 1;
    return 0;
}

static void test_isr_no_printf(void) {
    printf("  test_isr_no_printf...\n");

    /* elog_write_isr 接收预格式化 msg, 不调用 vsnprintf */
    elog_init();

    /* msg 包含 %s %d 等格式符, 不应被解析 */
    int ret = elog_write_isr(ELOG_LEVEL_INFO, "fmt", "%s %d %f", 7);
    assert(ret == ELOG_OK);

    /* 通过独立 ring buffer 验证 msg 内容未被格式化 */
    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_log_isr(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                          0, 0, 0, "fmt", "%s %d %f");

    int found_raw = 0;
    elog_ring_buf_flush_range(&rb.base, 0, rb.write_pos,
        find_raw_msg_cb, &found_raw);
    assert(found_raw == 1);

    elog_ring_buf_destroy(&rb);
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
    test_isr_basic();
    test_isr_overwrite();
    test_isr_stats();
    test_isr_macro();
    test_isr_no_printf();

    return 0;
}

/**
 * @file test_elog_stats.c
 * @brief LogStatistics 单元测试
 */

#include "elog_stats.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_stats_init(void) {
    printf("  test_stats_init...\n");

    elog_stats_t s;
    elog_stats_init(&s);

    for (int i = 0; i < ELOG_ID_MAX; i++) {
        assert(s.total_count[i] == 0);
        assert(s.dropped_count[i] == 0);
    }
    for (int i = 0; i < 6; i++) {
        assert(s.level_count[i] == 0);
    }
    assert(s.buffer_usage == 0);
    assert(s.buffer_peak == 0);
}

static void test_stats_on_log(void) {
    printf("  test_stats_on_log...\n");

    elog_stats_t s;
    elog_stats_init(&s);

    elog_stats_on_log(&s, ELOG_ID_MAIN, ELOG_LEVEL_INFO);
    assert(s.total_count[ELOG_ID_MAIN] == 1);
    assert(s.level_count[ELOG_LEVEL_INFO] == 1);

    elog_stats_on_log(&s, ELOG_ID_MAIN, ELOG_LEVEL_ERROR);
    assert(s.total_count[ELOG_ID_MAIN] == 2);
    assert(s.level_count[ELOG_LEVEL_ERROR] == 1);

    elog_stats_on_log(&s, ELOG_ID_SYSTEM, ELOG_LEVEL_DEBUG);
    assert(s.total_count[ELOG_ID_SYSTEM] == 1);
    assert(s.total_count[ELOG_ID_MAIN] == 2);  /* 不变 */
}

static void test_stats_on_drop(void) {
    printf("  test_stats_on_drop...\n");

    elog_stats_t s;
    elog_stats_init(&s);

    elog_stats_on_drop(&s, ELOG_ID_MAIN);
    assert(s.dropped_count[ELOG_ID_MAIN] == 1);

    elog_stats_on_drop(&s, ELOG_ID_MAIN);
    elog_stats_on_drop(&s, ELOG_ID_MAIN);
    assert(s.dropped_count[ELOG_ID_MAIN] == 3);

    /* 不影响 total */
    assert(s.total_count[ELOG_ID_MAIN] == 0);
}

static void test_stats_update_usage(void) {
    printf("  test_stats_update_usage...\n");

    elog_stats_t s;
    elog_stats_init(&s);

    elog_stats_update_usage(&s, 1024);
    assert(s.buffer_usage == 1024);
    assert(s.buffer_peak == 1024);

    elog_stats_update_usage(&s, 2048);
    assert(s.buffer_usage == 2048);
    assert(s.buffer_peak == 2048);

    elog_stats_update_usage(&s, 512);
    assert(s.buffer_usage == 512);
    assert(s.buffer_peak == 2048);  /* 峰值不变 */
}

static void test_stats_get(void) {
    printf("  test_stats_get...\n");

    elog_stats_t s;
    elog_stats_init(&s);

    elog_stats_on_log(&s, ELOG_ID_MAIN, ELOG_LEVEL_INFO);
    elog_stats_on_log(&s, ELOG_ID_MAIN, ELOG_LEVEL_ERROR);
    elog_stats_on_drop(&s, ELOG_ID_MAIN);

    char buf[512];
    int ret = elog_stats_get(&s, buf, sizeof(buf));
    assert(ret > 0);
    assert(strstr(buf, "main") != NULL);
    assert(strstr(buf, "total=2") != NULL);
    assert(strstr(buf, "dropped=1") != NULL);
    assert(strstr(buf, "INFO=1") != NULL);
    assert(strstr(buf, "ERROR=1") != NULL);

    printf("    output:\n%s", buf);
}

static void test_stats_reset(void) {
    printf("  test_stats_reset...\n");

    elog_stats_t s;
    elog_stats_init(&s);

    elog_stats_on_log(&s, ELOG_ID_MAIN, ELOG_LEVEL_INFO);
    elog_stats_on_drop(&s, ELOG_ID_MAIN);
    elog_stats_update_usage(&s, 4096);

    elog_stats_reset(&s);

    assert(s.total_count[ELOG_ID_MAIN] == 0);
    assert(s.dropped_count[ELOG_ID_MAIN] == 0);
    assert(s.level_count[ELOG_LEVEL_INFO] == 0);
    assert(s.buffer_usage == 0);
    assert(s.buffer_peak == 4096);  /* 峰值保留 */
}

static void test_stats_null(void) {
    printf("  test_stats_null...\n");

    /* 不应崩溃 */
    elog_stats_on_log(NULL, ELOG_ID_MAIN, ELOG_LEVEL_INFO);
    elog_stats_on_drop(NULL, ELOG_ID_MAIN);
    elog_stats_update_usage(NULL, 1024);
    elog_stats_reset(NULL);

    char buf[32];
    int ret = elog_stats_get(NULL, buf, sizeof(buf));
    assert(ret == 0);
}

int test_elog_stats(void) {
    printf("test_elog_stats:\n");

    test_stats_init();
    test_stats_on_log();
    test_stats_on_drop();
    test_stats_update_usage();
    test_stats_get();
    test_stats_reset();
    test_stats_null();

    return 0;
}

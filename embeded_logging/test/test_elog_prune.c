/**
 * @file test_elog_prune.c
 * @brief LogPrune 单元测试
 */

#include "elog_prune.h"
#include "elog_buf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_prune_init(void) {
    printf("  test_prune_init...\n");

    elog_prune_t p;
    elog_prune_init(&p, 90);
    assert(p.threshold_pct == 90);
    assert(p.high_count == 0);
    assert(p.low_count == 0);

    /* 默认阈值 */
    elog_prune_init(&p, 0);
    assert(p.threshold_pct == ELOG_PRUNE_THRESHOLD_PCT);
}

static void test_prune_set_rules_basic(void) {
    printf("  test_prune_set_rules_basic...\n");

    elog_prune_t p;
    elog_prune_init(&p, 90);

    int ret = elog_prune_load_rules(&p, "~elogd ~crash noisy verbose");
    assert(ret == ELOG_OK);
    assert(p.high_count == 2);
    assert(p.low_count == 2);

    /* 验证保护规则 */
    assert(elog_prune_is_protected(&p, "elogd") == true);
    assert(elog_prune_is_protected(&p, "crash") == true);
    assert(elog_prune_is_protected(&p, "noisy") == false);

    /* 验证低优先级规则 */
    assert(elog_prune_is_low_priority(&p, "noisy") == true);
    assert(elog_prune_is_low_priority(&p, "verbose") == true);
    assert(elog_prune_is_low_priority(&p, "elogd") == false);

    /* 未匹配 */
    assert(elog_prune_is_protected(&p, "other") == false);
    assert(elog_prune_is_low_priority(&p, "other") == false);
}

static void test_prune_set_rules_comma(void) {
    printf("  test_prune_set_rules_comma...\n");

    elog_prune_t p;
    elog_prune_init(&p, 90);

    /* 逗号分隔 */
    int ret = elog_prune_load_rules(&p, "~a,~b,c");
    assert(ret == ELOG_OK);
    assert(p.high_count == 2);
    assert(p.low_count == 1);
    assert(elog_prune_is_protected(&p, "a") == true);
    assert(elog_prune_is_protected(&p, "b") == true);
    assert(elog_prune_is_low_priority(&p, "c") == true);
}

static void test_prune_set_rules_replace(void) {
    printf("  test_prune_set_rules_replace...\n");

    elog_prune_t p;
    elog_prune_init(&p, 90);

    elog_prune_load_rules(&p, "~a ~b");
    assert(p.high_count == 2);

    /* 重新设置应清空旧规则 */
    elog_prune_load_rules(&p, "~c");
    assert(p.high_count == 1);
    assert(p.low_count == 0);
    assert(elog_prune_is_protected(&p, "a") == false);
    assert(elog_prune_is_protected(&p, "c") == true);
}

static void test_prune_get_rules(void) {
    printf("  test_prune_get_rules...\n");

    elog_prune_t p;
    elog_prune_init(&p, 90);

    elog_prune_load_rules(&p, "~elogd ~crash noisy");
    char buf[256];
    int ret = elog_prune_serialize_rules(&p, buf, sizeof(buf));
    assert(ret > 0);
    assert(strstr(buf, "~elogd") != NULL);
    assert(strstr(buf, "~crash") != NULL);
    assert(strstr(buf, "noisy") != NULL);

    printf("    rules: [%s]\n", buf);
}

static void test_prune_should_prune(void) {
    printf("  test_prune_should_prune...\n");

    elog_prune_t p;
    elog_prune_init(&p, 90);

    /* 未达阈值 */
    assert(elog_prune_should_prune(&p, 800, 1000) == false);

    /* 刚好达阈值 */
    assert(elog_prune_should_prune(&p, 900, 1000) == true);

    /* 超过阈值 */
    assert(elog_prune_should_prune(&p, 950, 1000) == true);

    /* 边界: 0 capacity */
    assert(elog_prune_should_prune(&p, 100, 0) == false);

    /* 边界: 100% */
    assert(elog_prune_should_prune(&p, 1000, 1000) == true);
}

static void test_prune_null(void) {
    printf("  test_prune_null...\n");

    assert(elog_prune_is_protected(NULL, "tag") == false);
    assert(elog_prune_is_low_priority(NULL, "tag") == false);
    assert(elog_prune_should_prune(NULL, 100, 200) == false);

    int ret = elog_prune_load_rules(NULL, "~a");
    assert(ret == ELOG_ERR_PARAM);

    ret = elog_prune_serialize_rules(NULL, NULL, 0);
    assert(ret == 0);
}

/* ===== 集成测试: Prune + RingBuffer ===== */

static int write_rb(elog_ring_buf_t* rb, const char* tag, const char* msg) {
    return rb->base.log(&rb->base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 1, 10, tag, msg);
}

static void test_prune_drop_low_priority(void) {
    printf("  test_prune_drop_low_priority...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 50);  /* 50% threshold */
    elog_prune_load_rules(&p, "~important noisy");

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* Fill buffer to > 50% with important entries */
    for (int i = 0; i < 8; i++) {
        write_rb(&rb, "important", "keep this");
    }

    /* noisy entry should be dropped because buffer is over threshold */
    int ret = write_rb(&rb, "noisy", "should drop");
    assert(ret == ELOG_ERR_PRUNED);

    /* important entry should still succeed (overwrite unprotected) */
    ret = write_rb(&rb, "important", "still goes in");
    assert(ret == ELOG_OK);

    elog_ring_buf_destroy(&rb);
}

static int count_flush_cb(const elog_msg_header_t* hdr,
                           const char* tag, const char* msg, void* user) {
    (void)hdr; (void)tag; (void)msg; (void)user;
    return 0;
}

static void test_prune_protected_survives(void) {
    printf("  test_prune_protected_survives...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 10);  /* very low threshold */
    elog_prune_load_rules(&p, "~crash noisy");

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* Fill with crash (protected) and noisy (low priority) entries */
    write_rb(&rb, "crash", "must keep 1");
    write_rb(&rb, "noisy", "can drop");
    write_rb(&rb, "crash", "must keep 2");
    write_rb(&rb, "noisy", "can drop 2");
    write_rb(&rb, "crash", "must keep 3");

    /* 5 entries in 256-byte buffer: no overwrite needed */
    assert(rb.count == 5);

    /* Verify flush works */
    int flushed = elog_ring_buf_flush(&rb.base, count_flush_cb, NULL);
    assert(flushed == 5);

    elog_ring_buf_destroy(&rb);
}

static void test_prune_below_threshold(void) {
    printf("  test_prune_below_threshold...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 99);  /* 99% threshold - very high */
    elog_prune_load_rules(&p, "noisy");

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* Write a few entries - should not trigger prune */
    int ret = write_rb(&rb, "noisy", "should NOT drop");
    assert(ret == ELOG_OK);
    ret = write_rb(&rb, "noisy", "also not dropped");
    assert(ret == ELOG_OK);

    assert(rb.count == 2);

    elog_ring_buf_destroy(&rb);
}

int test_elog_prune(void) {
    printf("test_elog_prune:\n");

    test_prune_init();
    test_prune_set_rules_basic();
    test_prune_set_rules_comma();
    test_prune_set_rules_replace();
    test_prune_get_rules();
    test_prune_should_prune();
    test_prune_null();
    test_prune_drop_low_priority();
    test_prune_protected_survives();
    test_prune_below_threshold();

    return 0;
}

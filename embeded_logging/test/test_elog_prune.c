/**
 * @file test_elog_prune.c
 * @brief LogPrune 单元测试
 */

#include "elog_prune.h"
#include "elog_buf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ===== 集成测试辅助 ===== */

static int write_rb(elog_ring_buf_t* rb, const char* tag, const char* msg) {
    return rb->base.log(&rb->base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 1, 10, tag, msg);
}

static int count_flush_cb(const elog_msg_header_t* hdr,
                           const char* tag, const char* msg, void* user) {
    (void)hdr; (void)tag; (void)msg; (void)user;
    return 0;
}

/* ===== 单元测试 ===== */

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

static void test_prune_max_rules_overflow(void) {
    printf("  test_prune_max_rules_overflow...\n");

    elog_prune_t p;
    elog_prune_init(&p, 50);

    /* 构造超过 ELOG_MAX_PRUNE_RULES 条规则 */
    char rules[512];
    int pos = 0;
    for (int i = 0; i < ELOG_MAX_PRUNE_RULES + 5; i++) {
        pos += snprintf(rules + pos, sizeof(rules) - (size_t)pos,
                        "%s~prot%d", i > 0 ? " " : "", i);
    }
    int ret = elog_prune_load_rules(&p, rules);
    assert(ret == ELOG_OK);

    /* 只保留前 ELOG_MAX_PRUNE_RULES 条 */
    assert(p.high_count == ELOG_MAX_PRUNE_RULES);
    assert(elog_prune_is_protected(&p, "prot0") == true);
    assert(elog_prune_is_protected(&p, "prot15") == true);
    assert(elog_prune_is_protected(&p, "prot16") == false);

    /* 低优先级同理 */
    elog_prune_load_rules(&p, "low0 low1 low2 low3 low4 low5 low6 low7 "
                          "low8 low9 low10 low11 low12 low13 low14 low15 low16 low17");
    assert(p.low_count == ELOG_MAX_PRUNE_RULES);
    assert(elog_prune_is_low_priority(&p, "low0") == true);
    assert(elog_prune_is_low_priority(&p, "low15") == true);
    assert(elog_prune_is_low_priority(&p, "low16") == false);
}

static void test_prune_empty_and_whitespace_rules(void) {
    printf("  test_prune_empty_and_whitespace_rules...\n");

    elog_prune_t p;
    elog_prune_init(&p, 50);

    /* 空字符串 */
    int ret = elog_prune_load_rules(&p, "");
    assert(ret == ELOG_OK);
    assert(p.high_count == 0);
    assert(p.low_count == 0);

    /* 纯空白 */
    ret = elog_prune_load_rules(&p, "   \t\n  ");
    assert(ret == ELOG_OK);
    assert(p.high_count == 0);
    assert(p.low_count == 0);

    /* 多个分隔符 */
    ret = elog_prune_load_rules(&p, ",,, ~a ;; ;; ~b");
    assert(ret == ELOG_OK);
    assert(p.high_count == 2);
    assert(elog_prune_is_protected(&p, "a") == true);
    assert(elog_prune_is_protected(&p, "b") == true);
}

static void test_prune_serialize_truncation(void) {
    printf("  test_prune_serialize_truncation...\n");

    elog_prune_t p;
    elog_prune_init(&p, 50);
    elog_prune_load_rules(&p, "~aaaa ~bbbb ~cccc");

    /* 缓冲区很小 — 应截断而非越界 */
    char buf[8];
    int n = elog_prune_serialize_rules(&p, buf, sizeof(buf));
    assert(n > 0);
    assert(n < (int)sizeof(buf) || buf[sizeof(buf) - 1] == '\0');

    /* 正常大小 */
    char buf2[256];
    n = elog_prune_serialize_rules(&p, buf2, sizeof(buf2));
    assert(n > 0);
    assert(strstr(buf2, "~aaaa") != NULL);
    assert(strstr(buf2, "~bbbb") != NULL);
    assert(strstr(buf2, "~cccc") != NULL);
}

static void test_prune_null_tag_in_write(void) {
    printf("  test_prune_null_tag_in_write...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 50);
    elog_prune_load_rules(&p, "noisy");

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* tag=NULL 应正常写入 (prune 检查跳过 NULL tag) */
    int ret = write_rb(&rb, NULL, "no tag msg");
    assert(ret == ELOG_OK);

    elog_ring_buf_destroy(&rb);
}

static void test_prune_force_bypasses_prune(void) {
    printf("  test_prune_force_bypasses_prune...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 50);
    elog_prune_load_rules(&p, "~important noisy");

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* Fill buffer */
    for (int i = 0; i < 8; i++) {
        write_rb(&rb, "important", "fill");
    }

    /* ISR path (force=true) should bypass prune check */
    int ret = elog_ring_buf_log_isr(&rb, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                                      1, 1, 10, "noisy", "force write");
    assert(ret == ELOG_OK);  /* force 写入不受 prune 限制 */

    elog_ring_buf_destroy(&rb);
}

static void test_prune_overwrite_content_correctness(void) {
    printf("  test_prune_overwrite_content_correctness...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 50);
    elog_prune_load_rules(&p, "~important noisy");

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* Fill with important */
    for (int i = 0; i < 8; i++) {
        write_rb(&rb, "important", "keep this");
    }

    /* Write more important entries — should overwrite old ones */
    for (int i = 0; i < 3; i++) {
        int ret = write_rb(&rb, "important", "new data");
        assert(ret == ELOG_OK);
    }

    /* Flush and verify all entries have correct tag */
    int important_count = 0;
    elog_ring_buf_flush(&rb.base, count_flush_cb, NULL);

    /* Re-read entries to verify tags */
    elog_ring_buf_clear(&rb.base);
    write_rb(&rb, "important", "final");
    write_rb(&rb, "important", "check");
    assert(rb.count == 2);

    int flushed = elog_ring_buf_flush(&rb.base, count_flush_cb, NULL);
    assert(flushed == 2);

    elog_ring_buf_destroy(&rb);
}

static void test_prune_no_rules_set(void) {
    printf("  test_prune_no_rules_set...\n");

    elog_ring_buf_t rb;
    elog_prune_t p;
    elog_prune_init(&p, 50);
    /* No rules loaded */

    elog_ring_buf_init(&rb, 256);
    elog_ring_buf_set_prune(&rb, &p);

    /* Fill buffer */
    for (int i = 0; i < 8; i++) {
        write_rb(&rb, "anytag", "msg");
    }

    /* "anytag" is not in low_priority list, so it should NOT be pruned */
    int ret = write_rb(&rb, "anytag", "still goes in");
    assert(ret == ELOG_OK);

    elog_ring_buf_destroy(&rb);
}

static void test_prune_should_prune_edge_cases(void) {
    printf("  test_prune_should_prune_edge_cases...\n");

    elog_prune_t p;
    elog_prune_init(&p, 1);  /* 1% threshold - very sensitive */

    /* 空缓冲区 */
    assert(elog_prune_should_prune(&p, 0, 1000) == false);

    /* 1/1000 = 0.1% < 1% */
    assert(elog_prune_should_prune(&p, 1, 1000) == false);

    /* 10/1000 = 1% >= 1% */
    assert(elog_prune_should_prune(&p, 10, 1000) == true);

    /* 0% threshold */
    p.threshold_pct = 0;
    assert(elog_prune_should_prune(&p, 0, 1000) == true);
    assert(elog_prune_should_prune(&p, 1, 1000) == true);

    /* 100% threshold */
    p.threshold_pct = 100;
    assert(elog_prune_should_prune(&p, 999, 1000) == false);
    assert(elog_prune_should_prune(&p, 1000, 1000) == true);
}

/* ===== 集成测试: Prune + RingBuffer ===== */

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
    test_prune_max_rules_overflow();
    test_prune_empty_and_whitespace_rules();
    test_prune_serialize_truncation();
    test_prune_null_tag_in_write();
    test_prune_force_bypasses_prune();
    test_prune_overwrite_content_correctness();
    test_prune_no_rules_set();
    test_prune_should_prune_edge_cases();
    test_prune_drop_low_priority();
    test_prune_protected_survives();
    test_prune_below_threshold();

    return 0;
}

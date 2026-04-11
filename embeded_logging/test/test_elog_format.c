/**
 * @file test_elog_format.c
 * @brief Formatter 单元测试
 */

#include "elog_format.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_format_text_basic(void) {
    printf("  test_format_text_basic...\n");

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_MAIN;
    hdr.level = ELOG_LEVEL_INFO;
    hdr.timestamp = 1704067200;  /* 2024-01-01 00:00:00 UTC */
    hdr.pid = 1234;
    hdr.tid = 567;
    hdr.line = 42;
    hdr.tag_len = 6;
    hdr.msg_len = 10;

    elog_format_ctx_t ctx;
    int ret = elog_format_text(&ctx, &hdr, "sensor", "temp=25.5");
    assert(ret > 0);
    assert(ctx.len > 0);
    assert(ctx.buf[ctx.len] == '\0');

    /* 应包含级别字符 I */
    assert(strstr(ctx.buf, "I") != NULL);
    /* 应包含 tag */
    assert(strstr(ctx.buf, "sensor") != NULL);
    /* 应包含消息 */
    assert(strstr(ctx.buf, "temp=25.5") != NULL);
    /* 应包含 PID */
    assert(strstr(ctx.buf, "1234") != NULL);

    printf("    output: [%s]\n", ctx.buf);
}

static void test_format_text_all_levels(void) {
    printf("  test_format_text_all_levels...\n");

    elog_level_t levels[] = {
        ELOG_LEVEL_VERBOSE, ELOG_LEVEL_DEBUG, ELOG_LEVEL_INFO,
        ELOG_LEVEL_WARN, ELOG_LEVEL_ERROR, ELOG_LEVEL_FATAL
    };
    const char* expected[] = {"V", "D", "I", "W", "E", "F"};

    for (int i = 0; i < 6; i++) {
        elog_msg_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.level = (uint8_t)levels[i];
        hdr.pid = 1;
        hdr.tid = 1;

        elog_format_ctx_t ctx;
        elog_format_text(&ctx, &hdr, "t", "m");

        /* 应包含对应的级别字符 */
        assert(strstr(ctx.buf, expected[i]) != NULL);
    }
}

static void test_format_binary(void) {
    printf("  test_format_binary...\n");

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_MAIN;
    hdr.level = ELOG_LEVEL_ERROR;
    hdr.tag_len = 4;
    hdr.msg_len = 5;

    elog_format_ctx_t ctx;
    int ret = elog_format_binary(&ctx, &hdr, "test", "hello");
    assert(ret == (int)(sizeof(elog_msg_header_t) + 4 + 5));
    assert(ctx.len == ret);

    /* 验证 header 在前面 */
    assert(memcmp(ctx.buf, &hdr, sizeof(hdr)) == 0);

    /* 验证 tag */
    assert(memcmp(ctx.buf + sizeof(hdr), "test", 4) == 0);

    /* 验证 msg */
    assert(memcmp(ctx.buf + sizeof(hdr) + 4, "hello", 5) == 0);
}

static void test_format_binary_null(void) {
    printf("  test_format_binary_null...\n");

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.tag_len = 0;
    hdr.msg_len = 0;

    elog_format_ctx_t ctx;
    int ret = elog_format_binary(&ctx, &hdr, NULL, NULL);
    assert(ret == (int)sizeof(elog_msg_header_t));
}

static void test_format_timestamp(void) {
    printf("  test_format_timestamp...\n");

    char buf[32];

    /* 先测试实际输出格式 */
    int ret = elog_format_timestamp(1704067200, buf, sizeof(buf));
    assert(ret > 0);
    /* 格式: "MM-DD HH:MM:SS" = 14 chars (不含 NUL) */
    assert(ret == 14);
    assert(buf[2] == '-');
    assert(buf[5] == ' ');
    assert(buf[8] == ':');
    assert(buf[11] == ':');

    /* 测试短 buffer */
    ret = elog_format_timestamp(1704067200, buf, 5);
    assert(ret == 0);

    /* NULL */
    ret = elog_format_timestamp(0, NULL, 32);
    assert(ret == 0);
}

static void test_format_text_color(void) {
    printf("  test_format_text_color...\n");

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.level = ELOG_LEVEL_ERROR;

    elog_format_ctx_t ctx;
    elog_format_text(&ctx, &hdr, "t", "m");

#if ELOG_COLOR_ENABLE
    /* 应包含 ANSI 颜色码 */
    assert(strstr(ctx.buf, "\033[") != NULL);
    assert(strstr(ctx.buf, "\033[0m") != NULL);
#endif
}

static void test_format_text_long_msg(void) {
    printf("  test_format_text_long_msg...\n");

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.level = ELOG_LEVEL_INFO;

    char long_msg[ELOG_MAX_MSG_LEN * 2];
    memset(long_msg, 'X', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';

    elog_format_ctx_t ctx;
    int ret = elog_format_text(&ctx, &hdr, "t", long_msg);
    assert(ret > 0);
    /* 应被截断 */
    assert(ctx.len < (int)(ELOG_MAX_FORMAT_LEN - 1));
}

static void test_format_null_params(void) {
    printf("  test_format_null_params...\n");

    elog_format_ctx_t ctx;

    /* NULL hdr */
    int ret = elog_format_text(&ctx, NULL, "t", "m");
    assert(ret == ELOG_ERR_PARAM);

    ret = elog_format_binary(&ctx, NULL, "t", "m");
    assert(ret == ELOG_ERR_PARAM);

    /* NULL ctx */
    ret = elog_format_text(NULL, &((elog_msg_header_t){0}), "t", "m");
    assert(ret == ELOG_ERR_PARAM);
}

int test_elog_format(void) {
    printf("test_elog_format:\n");

    test_format_text_basic();
    test_format_text_all_levels();
    test_format_binary();
    test_format_binary_null();
    test_format_timestamp();
    test_format_text_color();
    test_format_text_long_msg();
    test_format_null_params();

    return 0;
}

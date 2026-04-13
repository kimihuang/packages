/**
 * @file test_elog_event.c
 * @brief Event 日志单元测试
 */

#include "elog_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

static void test_event_create_destroy(void) {
    printf("  test_event_create_destroy...\n");

    elog_event_ctx_t* ctx = elog_event_create(1001);
    assert(ctx != NULL);
    assert(elog_event_id(ctx) == 1001);

    elog_event_destroy(ctx);

    /* NULL destroy 不崩溃 */
    elog_event_destroy(NULL);
}

static void test_event_int32(void) {
    printf("  test_event_int32...\n");

    elog_event_ctx_t* ctx = elog_event_create(2001);
    assert(ctx != NULL);

    int ret = elog_event_add_int32(ctx, 42);
    assert(ret == ELOG_OK);

    size_t len;
    const uint8_t* data = elog_event_data(ctx, &len);
    assert(data != NULL);
    /* 单元素: 剥离 LIST 包装, 只有 INT32 type(1B) + value(4B) = 5B */
    assert(len == 5);
    assert(data[0] == ELOG_EVENT_TYPE_INT32);

    int32_t val;
    memcpy(&val, data + 1, sizeof(val));
    assert(val == 42);

    elog_event_destroy(ctx);
}

static void test_event_int64(void) {
    printf("  test_event_int64...\n");

    elog_event_ctx_t* ctx = elog_event_create(2002);

    elog_event_add_int64(ctx, 1234567890123LL);

    size_t len;
    const uint8_t* data = elog_event_data(ctx, &len);
    assert(len == 9);
    assert(data[0] == ELOG_EVENT_TYPE_INT64);

    int64_t val;
    memcpy(&val, data + 1, sizeof(val));
    assert(val == 1234567890123LL);

    elog_event_destroy(ctx);
}

static void test_event_float(void) {
    printf("  test_event_float...\n");

    elog_event_ctx_t* ctx = elog_event_create(2003);

    elog_event_add_float(ctx, 3.14f);

    size_t len;
    const uint8_t* data = elog_event_data(ctx, &len);
    assert(len == 5);
    assert(data[0] == ELOG_EVENT_TYPE_FLOAT);

    float val;
    memcpy(&val, data + 1, sizeof(val));
    assert(val >= 3.13f && val <= 3.15f);

    elog_event_destroy(ctx);
}

static void test_event_string(void) {
    printf("  test_event_string...\n");

    elog_event_ctx_t* ctx = elog_event_create(2004);

    int ret = elog_event_add_string(ctx, "hello");
    assert(ret == ELOG_OK);

    size_t len;
    const uint8_t* data = elog_event_data(ctx, &len);
    assert(data != NULL);
    /* STRING: type(1B) + len(4B) + "hello"(5B) = 10B */
    assert(len == 10);
    assert(data[0] == ELOG_EVENT_TYPE_STRING);

    uint32_t slen;
    memcpy(&slen, data + 1, sizeof(slen));
    assert(slen == 5);
    assert(memcmp(data + 5, "hello", 5) == 0);

    elog_event_destroy(ctx);
}

static void test_event_multi_elements(void) {
    printf("  test_event_multi_elements...\n");

    elog_event_ctx_t* ctx = elog_event_create(3001);

    elog_event_add_int32(ctx, 100);
    elog_event_add_float(ctx, 2.5f);
    elog_event_add_string(ctx, "ok");

    size_t len;
    const uint8_t* data = elog_event_data(ctx, &len);
    assert(data != NULL);

    /* 多元素: 保留 LIST 包装 = event_id(4) + type(1) + count(1) + elements */
    /* INT32(5) + FLOAT(5) + STRING("ok": 5+2=7) = 17 */
    /* 总: 4 + 1 + 1 + 17 = 23 */
    assert(len == 23);

    /* 验证 LIST type */
    assert(data[4] == ELOG_EVENT_TYPE_LIST);
    assert(data[5] == 3);  /* count */

    /* 验证第一个元素 INT32 */
    assert(data[6] == ELOG_EVENT_TYPE_INT32);

    printf("    (multi elements: %zu bytes)\n", len);

    elog_event_destroy(ctx);
}

static void test_event_overflow(void) {
    printf("  test_event_overflow...\n");

    elog_event_ctx_t* ctx = elog_event_create(4001);

    /* 填满 buffer */
    int overflowed = 0;
    for (int i = 0; i < 1000; i++) {
        char str[32];
        snprintf(str, sizeof(str), "str_%d_padding", i);
        int ret = elog_event_add_string(ctx, str);
        if (ret == ELOG_ERR_OVERFLOW) {
            overflowed = 1;
            break;
        }
    }
    assert(overflowed == 1);

    /* 溢出后继续添加应返回错误 */
    int ret = elog_event_add_int32(ctx, 1);
    assert(ret == ELOG_ERR_OVERFLOW);

    elog_event_destroy(ctx);
}

static void test_event_null(void) {
    printf("  test_event_null...\n");

    /* NULL ctx 不崩溃 */
    int ret = elog_event_add_int32(NULL, 1);
    assert(ret != ELOG_OK);

    ret = elog_event_add_string(NULL, "test");
    assert(ret != ELOG_OK);

    const uint8_t* data = elog_event_data(NULL, NULL);
    assert(data == NULL);

    size_t len;
    data = elog_event_data(NULL, &len);
    assert(data == NULL);
    assert(len == 0);

    assert(elog_event_id(NULL) == 0);
}

/* ===== Parser 测试 ===== */

static void test_event_parser(void) {
    printf("  test_event_parser...\n");

    elog_event_ctx_t* ctx = elog_event_create(5001);
    elog_event_add_int32(ctx, 100);
    elog_event_add_float(ctx, 2.5f);
    elog_event_add_string(ctx, "hello");

    size_t data_len;
    const uint8_t* data = elog_event_data(ctx, &data_len);

    /* 多元素: data = [event_id(4)][LIST type(1)][count(1)][elements...] */
    const uint8_t* payload = data + 4 + 2;
    size_t payload_len = data_len - 4 - 2;

    elog_event_parser_t parser;
    elog_event_parser_init(&parser, payload, payload_len);

    elog_event_value_t val;
    int ret = elog_event_parser_next(&parser, &val);
    assert(ret == ELOG_OK && val.type == ELOG_EVENT_TYPE_INT32 && val.int32_val == 100);

    ret = elog_event_parser_next(&parser, &val);
    assert(ret == ELOG_OK && val.type == ELOG_EVENT_TYPE_FLOAT);
    assert(val.float_val >= 2.49f && val.float_val <= 2.51f);

    ret = elog_event_parser_next(&parser, &val);
    assert(ret == ELOG_OK && val.type == ELOG_EVENT_TYPE_STRING);
    assert(val.str_len == 5 && memcmp(val.str_val, "hello", 5) == 0);

    ret = elog_event_parser_next(&parser, &val);
    assert(ret != ELOG_OK);

    /* NULL 参数不崩溃 */
    elog_event_parser_init(NULL, NULL, 0);
    ret = elog_event_parser_next(NULL, NULL);
    assert(ret != ELOG_OK);

    elog_event_destroy(ctx);
    printf("    (parser ok)\n");
}

/* ===== 嵌套列表测试 ===== */

static void test_event_nested_list(void) {
    printf("  test_event_nested_list...\n");

    elog_event_ctx_t* ctx = elog_event_create(6001);
    elog_event_add_int32(ctx, 1);
    elog_event_list_begin(ctx);
    elog_event_add_string(ctx, "nested_a");
    elog_event_add_int32(ctx, 99);
    elog_event_list_end(ctx);
    elog_event_add_string(ctx, "top_end");

    size_t len;
    const uint8_t* data = elog_event_data(ctx, &len);

    assert(len > 0);
    assert(data[4] == ELOG_EVENT_TYPE_LIST);
    assert(data[5] == 3);

    /* 第一个元素: INT32(1) = 5 bytes, offset 6 */
    assert(data[6] == ELOG_EVENT_TYPE_INT32);

    /* 第二个元素: nested LIST at offset 11 (6+5) */
    assert(data[11] == ELOG_EVENT_TYPE_LIST);
    assert(data[12] == 2);

    elog_event_destroy(ctx);
    printf("    (nested list: %zu bytes)\n", len);
}

/* ===== 嵌套深度限制测试 ===== */

static void test_event_list_depth_limit(void) {
    printf("  test_event_list_depth_limit...\n");

    elog_event_ctx_t* ctx = elog_event_create(7001);
    for (int i = 0; i < ELOG_EVENT_LIST_DEPTH; i++) {
        int ret = elog_event_list_begin(ctx);
        assert(ret == ELOG_OK);
    }
    /* 超出深度 */
    int ret = elog_event_list_begin(ctx);
    assert(ret == ELOG_ERR_OVERFLOW);

    /* 关闭所有 */
    for (int i = 0; i < ELOG_EVENT_LIST_DEPTH; i++) {
        elog_event_list_end(ctx);
    }
    /* 多关一次 */
    ret = elog_event_list_end(ctx);
    assert(ret == ELOG_ERR_PARAM);

    elog_event_destroy(ctx);
}

int test_elog_event(void) {
    printf("test_elog_event:\n");

    test_event_create_destroy();
    test_event_int32();
    test_event_int64();
    test_event_float();
    test_event_string();
    test_event_multi_elements();
    test_event_overflow();
    test_event_null();
    test_event_parser();
    test_event_nested_list();
    test_event_list_depth_limit();

    return 0;
}

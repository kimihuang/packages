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

    return 0;
}

/**
 * @file test_elog_buf.c
 * @brief RingLogBuffer 单元测试
 */

#include "elog_buf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* flush 回调: 统计条目数 */
typedef struct {
    int count;
    elog_msg_header_t last_hdr;
    char last_tag[ELOG_MAX_TAG_LEN];
    char last_msg[ELOG_MAX_MSG_LEN];
} flush_test_ctx_t;

static int flush_callback(const elog_msg_header_t* hdr,
                           const char* tag, const char* msg, void* user) {
    flush_test_ctx_t* ctx = (flush_test_ctx_t*)user;
    ctx->count++;
    if (hdr) memcpy(&ctx->last_hdr, hdr, sizeof(*hdr));
    if (tag) { strncpy(ctx->last_tag, tag, ELOG_MAX_TAG_LEN - 1); ctx->last_tag[ELOG_MAX_TAG_LEN - 1] = '\0'; }
    if (msg) { strncpy(ctx->last_msg, msg, ELOG_MAX_MSG_LEN - 1); ctx->last_msg[ELOG_MAX_MSG_LEN - 1] = '\0'; }
    return 0;  /* 继续 flush */
}

static void test_init_destroy(void) {
    printf("  test_init_destroy...\n");

    elog_ring_buf_t rb;
    int ret = elog_ring_buf_init(&rb, 4096);
    assert(ret == ELOG_OK);
    assert(rb.buffer != NULL);
    assert(rb.buf_capacity == 4096);
    assert(rb.count == 0);
    assert(rb.base.is_empty(&rb.base) == true);

    elog_ring_buf_destroy(&rb);
}

static void test_static_init(void) {
    printf("  test_static_init...\n");

    elog_ring_buf_t rb;
    uint8_t buf[2048];
    int ret = elog_ring_buf_init_static(&rb, buf, sizeof(buf));
    assert(ret == ELOG_OK);
    assert(rb.buffer == buf);
    assert(rb.buf_capacity == 2048);

    /* 注意: 不调用 destroy (静态 buffer) */
    (void)0;
}

static void test_write_and_flush(void) {
    printf("  test_write_and_flush...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    /* 写入 */
    int ret = rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                          100, 200, 42, "sensor", "temp=25");
    assert(ret == ELOG_OK);
    assert(rb.base.is_empty(&rb.base) == false);

    /* flush */
    flush_test_ctx_t ctx = {0};
    int flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 1);
    assert(ctx.count == 1);
    assert(strcmp(ctx.last_tag, "sensor") == 0);
    assert(strcmp(ctx.last_msg, "temp=25") == 0);
    assert(ctx.last_hdr.level == ELOG_LEVEL_INFO);
    assert(ctx.last_hdr.line == 42);

    /* flush 后应清空 */
    flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 0);

    elog_ring_buf_destroy(&rb);
}

static void test_multiple_writes(void) {
    printf("  test_multiple_writes...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    for (int i = 0; i < 50; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "msg_%d", i);
        rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                    100, 200, i, "test", msg);
    }

    flush_test_ctx_t ctx = {0};
    int flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 50);
    assert(ctx.count == 50);

    elog_ring_buf_destroy(&rb);
}

static void test_overwrite(void) {
    printf("  test_overwrite...\n");

    /* 小 buffer 强制覆写 */
    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 512);  /* 很小 */

    flush_test_ctx_t ctx = {0};

    /* 写入大量数据直到覆写 */
    for (int i = 0; i < 100; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "overflow_msg_%d_with_padding", i);
        rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                    100, 200, i, "overflow", msg);
    }

    /* flush 应该得到部分数据 (旧的被覆写) */
    int flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed > 0);

    /* 512B buffer, 每条 ~60B aligned, 只能容纳约 8 条。
       100 次写入后 buffer 中只保留最近 ~8 条 */
    assert(flushed <= 10);

    /* 验证最后写入的消息是最新的 */
    int last_i = 99;
    char expected_msg[64];
    snprintf(expected_msg, sizeof(expected_msg), "overflow_msg_%d_with_padding", last_i);
    assert(strcmp(ctx.last_msg, expected_msg) == 0);
    assert(strcmp(ctx.last_tag, "overflow") == 0);

    printf("    (flushed %d of 100 entries, buffer capacity=512, count=%zu)\n",
           flushed, rb.count);

    /* 二次 flush 应该返回 0 (已全部读出) */
    flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 0);

    elog_ring_buf_destroy(&rb);
}

static void test_overwrite_flush_consistency(void) {
    printf("  test_overwrite_flush_consistency...\n");

    /* 验证: 写入 → flush → 再写入 → flush，flush 后 buffer 应为空 */
    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    flush_test_ctx_t ctx = {0};

    /* 填满 + 覆写 */
    for (int i = 0; i < 50; i++) {
        rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 2, i, "t", "m");
    }
    int flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed > 0);
    assert(flushed < 50);

    /* flush 后再 flush 应为空 */
    flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 0);

    elog_ring_buf_destroy(&rb);
}

static void test_cross_boundary_write(void) {
    printf("  test_cross_boundary_write...\n");

    /* 手动触发跨边界写入: 先写入若干条直到 write_pos 接近末尾，
       然后写入一条正好跨过边界的大条目 */
    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    flush_test_ctx_t ctx = {0};

    /* 每条: 4 + ALIGN4(16+1+1) = 4 + 20 = 24 bytes.
       256 / 24 = 10 条余 16 bytes。
       写 10 条后 write_pos = 240, remaining = 16.
       第 11 条(36 bytes)超过剩余空间，触发覆写：淘汰第 1 条，buffer 中剩 10 条。 */
    for (int i = 0; i < 10; i++) {
        char msg[2] = { 'A' + i, '\0' };
        rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 2, i, "t", msg);
    }

    /* 第 11 条: 跨边界写入，且触发覆写 */
    rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_WARN, 1, 2, 99, "cross", "boundary");

    /* flush 得到 10 条 (第 1 条被覆写淘汰) */
    int flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 10);
    assert(ctx.count == 10);

    /* 验证第一条是原来的第 2 条 (i=1), 最后一条是新的跨边界条目 */
    assert(ctx.last_hdr.level == ELOG_LEVEL_WARN);
    assert(ctx.last_hdr.line == 99);
    assert(strcmp(ctx.last_tag, "cross") == 0);
    assert(strcmp(ctx.last_msg, "boundary") == 0);

    printf("    (10 entries flushed, 1 evicted by overwrite + cross-boundary entry OK)\n");
}

static void test_clear(void) {
    printf("  test_clear...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 2, 3, "t", "m");
    rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 2, 3, "t", "m");

    rb.base.clear(&rb.base);
    assert(rb.base.is_empty(&rb.base) == true);
    assert(rb.count == 0);

    flush_test_ctx_t ctx = {0};
    int flushed = rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(flushed == 0);

    elog_ring_buf_destroy(&rb);
}

static void test_size_capacity(void) {
    printf("  test_size_capacity...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 8192);

    assert(rb.base.capacity(&rb.base) == 8192);
    assert(rb.base.size(&rb.base) == 0);

    rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 2, 3, "tag", "hello");
    size_t sz = rb.base.size(&rb.base);
    assert(sz > 0);
    assert(sz < 8192);

    printf("    (size after 1 entry: %zu bytes)\n", sz);

    elog_ring_buf_destroy(&rb);
}

static void test_null_params(void) {
    printf("  test_null_params...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    /* NULL tag/msg 应该不崩溃 */
    rb.base.log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 0, 0, 0, NULL, NULL);

    flush_test_ctx_t ctx = {0};
    rb.base.flush(&rb.base, flush_callback, &ctx);
    assert(ctx.count == 1);

    /* NULL callback */
    int ret = rb.base.flush(&rb.base, NULL, NULL);
    assert(ret == ELOG_ERR_PARAM);

    elog_ring_buf_destroy(&rb);
}

int test_elog_buf(void) {
    printf("test_elog_buf:\n");

    test_init_destroy();
    test_static_init();
    test_write_and_flush();
    test_multiple_writes();
    test_overwrite();
    test_overwrite_flush_consistency();
    test_cross_boundary_write();
    test_clear();
    test_size_capacity();
    test_null_params();

    return 0;
}

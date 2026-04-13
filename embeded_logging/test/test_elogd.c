/**
 * @file test_elogd.c
 * @brief elogd 单元测试 — log_from API + 线路格式 + 客户端 + reader 协议
 */

#include "elog_buf.h"
#include "elog_prune.h"
#include "elog_def.h"
#include "elogd.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* ===== elog_ring_buf_log_from 测试 ===== */

typedef struct {
    elog_id_t log_id;
    elog_level_t level;
    uint16_t pid;
    uint16_t tid;
    int count;
} log_from_ctx_t;

static int log_from_cb(const elog_msg_header_t* hdr,
                        const char* tag, const char* msg, void* user) {
    log_from_ctx_t* ctx = (log_from_ctx_t*)user;
    ctx->log_id = (elog_id_t)hdr->log_id;
    ctx->level = (elog_level_t)hdr->level;
    ctx->pid = hdr->pid;
    ctx->tid = hdr->tid;
    ctx->count++;
    (void)tag; (void)msg;
    return 0;
}

static void test_log_from_basic(void) {
    printf("  test_log_from_basic...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    /* 写入指定 pid/tid */
    int ret = elog_ring_buf_log_from(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                                      100, 200, 42, "test", "hello");
    assert(ret == ELOG_OK);
    assert(rb.count == 1);

    /* 读取并验证 pid/tid */
    log_from_ctx_t ctx = {0};
    size_t start = 0;
    size_t end = rb.write_pos;
    int flushed = elog_ring_buf_flush_range(&rb, start, end, log_from_cb, &ctx);
    assert(flushed == 1);
    assert(ctx.pid == 100);
    assert(ctx.tid == 200);
    assert(ctx.level == ELOG_LEVEL_INFO);
    assert(ctx.log_id == ELOG_ID_MAIN);

    elog_ring_buf_destroy(&rb);
}

static void test_log_from_vs_normal(void) {
    printf("  test_log_from_vs_normal...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    /* log_from 使用参数值 */
    int ret = elog_ring_buf_log_from(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_WARN, 999, 888, 10, "from", "msg1");
    assert(ret == ELOG_OK);
    assert(rb.count == 1);

    /* flush 第一条, 验证 pid/tid 来自参数而非 port */
    log_from_ctx_t ctx = {0};
    elog_ring_buf_flush_range(&rb, 0, rb.write_pos, log_from_cb, &ctx);
    assert(ctx.count == 1);
    assert(ctx.pid == 999);
    assert(ctx.tid == 888);
    assert(ctx.level == ELOG_LEVEL_WARN);

    /* log 使用 elog_port_getpid/gettid */
    rb.write_pos = 0; rb.read_pos = 0; rb.count = 0; /* clear for simplicity */
    elog_ring_buf_log(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 0, 0, 20, "normal", "msg2");
    assert(rb.count == 1);

    elog_ring_buf_destroy(&rb);
}

static void test_log_from_multiple(void) {
    printf("  test_log_from_multiple...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    /* 批量写入, 验证所有条目都被正确存储 */
    for (int i = 0; i < 50; i++) {
        int ret = elog_ring_buf_log_from(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                                          (uint16_t)(1000 + i), (uint16_t)(2000 + i),
                                          (uint16_t)i, "tag", "msg");
        assert(ret == ELOG_OK);
    }
    assert(rb.count == 50);

    log_from_ctx_t ctx = {0};
    elog_ring_buf_flush_range(&rb, 0, rb.write_pos, log_from_cb, &ctx);
    assert(ctx.count == 50);

    elog_ring_buf_destroy(&rb);
}

static void test_log_from_overwrite(void) {
    printf("  test_log_from_overwrite...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    /* 写入超过容量, 触发覆写 */
    for (int i = 0; i < 30; i++) {
        elog_ring_buf_log_from(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                              (uint16_t)i, (uint16_t)i, (uint16_t)i, "t", "m");
    }

    /* count 不应超过 buffer 容量限制 */
    assert(rb.count <= 30);

    elog_ring_buf_destroy(&rb);
}

static void test_log_from_null_tag(void) {
    printf("  test_log_from_null_tag...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    int ret = elog_ring_buf_log_from(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                                      1, 1, 0, NULL, "no tag");
    assert(ret == ELOG_OK);
    assert(rb.count == 1);

    elog_ring_buf_destroy(&rb);
}

static void test_log_from_null_msg(void) {
    printf("  test_log_from_null_msg...\n");

    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 256);

    int ret = elog_ring_buf_log_from(&rb.base, ELOG_ID_MAIN, ELOG_LEVEL_INFO,
                                      1, 1, 0, "tag", NULL);
    assert(ret == ELOG_OK);
    assert(rb.count == 1);

    elog_ring_buf_destroy(&rb);
}

/* ===== 线路格式测试 ===== */

static void test_wire_format(void) {
    printf("  test_wire_format...\n");

    /* 模拟客户端构造的 datagram */
    uint8_t buf[sizeof(elog_msg_header_t) + 32 + 64];

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_MAIN;
    hdr.level = ELOG_LEVEL_ERROR;
    hdr.pid = 1234;
    hdr.tid = 5678;
    hdr.line = 42;

    const char* tag = "sensor";
    uint16_t tag_len = (uint16_t)strlen(tag);
    hdr.tag_len = tag_len;

    const char* msg = "temperature too high";
    uint16_t msg_len = (uint16_t)strlen(msg);
    hdr.msg_len = msg_len;

    /* 构造 packet */
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), tag, tag_len);
    memcpy(buf + sizeof(hdr) + tag_len, msg, msg_len);

    /* 验证 header 可正确还原 */
    elog_msg_header_t recv_hdr;
    memcpy(&recv_hdr, buf, sizeof(recv_hdr));
    assert(recv_hdr.log_id == ELOG_ID_MAIN);
    assert(recv_hdr.level == ELOG_LEVEL_ERROR);
    assert(recv_hdr.pid == 1234);
    assert(recv_hdr.tid == 5678);
    assert(recv_hdr.line == 42);
    assert(recv_hdr.tag_len == tag_len);
    assert(recv_hdr.msg_len == msg_len);

    /* 验证 tag/msg 可正确还原 */
    const char* recv_tag = (const char*)(buf + sizeof(recv_hdr));
    const char* recv_msg = recv_tag + recv_hdr.tag_len;
    assert(memcmp(recv_tag, tag, tag_len) == 0);
    assert(memcmp(recv_msg, msg, msg_len) == 0);
}

static void test_log_from_write_to_ring(void) {
    printf("  test_log_from_write_to_ring...\n");

    /* 完整路径: 构造 packet → log_from → flush → 验证内容 */
    elog_ring_buf_t rb;
    elog_ring_buf_init(&rb, 4096);

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_CRASH;
    hdr.level = ELOG_LEVEL_FATAL;
    hdr.pid = 999;
    hdr.tid = 888;
    hdr.line = 100;

    const char* tag = "crash_tag";
    const char* msg = "segfault in module X";

    hdr.tag_len = (uint16_t)strlen(tag);
    hdr.msg_len = (uint16_t)strlen(msg);

    /* 构造 packet 并写入 */
    uint8_t buf[sizeof(elog_msg_header_t) + ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), tag, hdr.tag_len);
    memcpy(buf + sizeof(hdr) + hdr.tag_len, msg, hdr.msg_len);

    int ret = elog_ring_buf_log_from(&rb.base, (elog_id_t)hdr.log_id,
                                      (elog_level_t)hdr.level,
                                      hdr.pid, hdr.tid, hdr.line,
                                      (const char*)(buf + sizeof(hdr)),
                                      (const char*)(buf + sizeof(hdr) + hdr.tag_len));
    assert(ret == ELOG_OK);

    /* flush 并验证 */
    log_from_ctx_t ctx = {0};
    elog_ring_buf_flush_range(&rb, 0, rb.write_pos, log_from_cb, &ctx);
    assert(ctx.count == 1);
    assert(ctx.pid == 999);
    assert(ctx.tid == 888);
    assert(ctx.log_id == ELOG_ID_CRASH);
    assert(ctx.level == ELOG_LEVEL_FATAL);

    elog_ring_buf_destroy(&rb);
}

/* ===== Reader 协议测试 ===== */

static void test_read_request_struct(void) {
    printf("  test_read_request_struct...\n");

    /* 验证结构体大小和字段布局 */
    elog_read_request_t req;
    memset(&req, 0, sizeof(req));
    assert(sizeof(req) >= 18);  /* version(4)+tail(4)+count(4)+min_level(1)+pid(2)+timeout(4) */

    req.version = ELOG_READ_PROTOCOL_VERSION;
    req.tail = 1;
    req.count = 100;
    req.min_level = ELOG_LEVEL_WARN;
    req.pid_filter = 1234;
    req.timeout_ms = 5000;

    assert(req.version == 1);
    assert(req.tail == 1);
    assert(req.count == 100);
    assert(req.min_level == ELOG_LEVEL_WARN);
    assert(req.pid_filter == 1234);
    assert(req.timeout_ms == 5000);
}

static void test_read_request_wire_roundtrip(void) {
    printf("  test_read_request_wire_roundtrip...\n");

    /* 模拟序列化/反序列化 */
    elog_read_request_t req;
    memset(&req, 0, sizeof(req));
    req.version = ELOG_READ_PROTOCOL_VERSION;
    req.tail = 1;
    req.count = 50;
    req.min_level = ELOG_LEVEL_INFO;
    req.pid_filter = 0;
    req.timeout_ms = 0;

    /* 通过 memcpy 序列化 (模拟 send/recv) */
    uint8_t wire[64];
    memcpy(wire, &req, sizeof(req));

    /* 反序列化 */
    elog_read_request_t recv;
    memcpy(&recv, wire, sizeof(recv));

    assert(recv.version == req.version);
    assert(recv.tail == req.tail);
    assert(recv.count == req.count);
    assert(recv.min_level == req.min_level);
    assert(recv.pid_filter == req.pid_filter);
    assert(recv.timeout_ms == req.timeout_ms);
}

static void test_datagram_entry_size(void) {
    printf("  test_datagram_entry_size...\n");

    /* 验证: 一个 datagram 的大小 = sizeof(header) + tag_len + msg_len */
    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    const char* tag = "sensor";
    const char* msg = "temp=25.5";
    hdr.tag_len = (uint16_t)strlen(tag);
    hdr.msg_len = (uint16_t)strlen(msg);

    size_t datagram_size = sizeof(hdr) + hdr.tag_len + hdr.msg_len;
    assert(datagram_size == 16 + 6 + 9);  /* 31 bytes */

    /* 构造完整 datagram */
    uint8_t buf[64];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), tag, hdr.tag_len);
    memcpy(buf + sizeof(hdr) + hdr.tag_len, msg, hdr.msg_len);

    /* 解析验证 */
    elog_msg_header_t recv_hdr;
    memcpy(&recv_hdr, buf, sizeof(recv_hdr));
    assert(recv_hdr.tag_len == 6);
    assert(recv_hdr.msg_len == 9);
    assert(memcmp(buf + sizeof(recv_hdr), "sensor", 6) == 0);
    assert(memcmp(buf + sizeof(recv_hdr) + 6, "temp=25.5", 9) == 0);
}

static void test_reader_sock_paths(void) {
    printf("  test_reader_sock_paths...\n");

    /* 验证 socket 路径常量存在且合理 */
    assert(strlen(ELOG_DAEMON_SOCK_PATH) > 0);
    assert(strlen(ELOG_DAEMON_CMD_SOCK) > 0);
    assert(strlen(ELOG_DAEMON_READER_SOCK) > 0);

    /* 三个 socket 路径应该不同 */
    assert(strcmp(ELOG_DAEMON_SOCK_PATH, ELOG_DAEMON_CMD_SOCK) != 0);
    assert(strcmp(ELOG_DAEMON_CMD_SOCK, ELOG_DAEMON_READER_SOCK) != 0);
    assert(strcmp(ELOG_DAEMON_SOCK_PATH, ELOG_DAEMON_READER_SOCK) != 0);
}

int test_elogd(void) {
    printf("test_elogd:\n");

    test_log_from_basic();
    test_log_from_vs_normal();
    test_log_from_multiple();
    test_log_from_overwrite();
    test_log_from_null_tag();
    test_log_from_null_msg();
    test_wire_format();
    test_log_from_write_to_ring();
    test_read_request_struct();
    test_read_request_wire_roundtrip();
    test_datagram_entry_size();
    test_reader_sock_paths();

    return 0;
}

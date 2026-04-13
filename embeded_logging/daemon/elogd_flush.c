/**
 * @file elogd_flush.c
 * @brief Flusher — 从 ring buffer 读取日志，格式化后写入 file transport
 */

#include "elogd.h"
#include "elog_buf.h"
#include "elog_transport_file.h"
#include "elog_format.h"
#include "elog_def.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

extern elog_ring_buf_t* g_daemon_rb;
extern volatile bool g_daemon_running;

typedef struct {
    elog_file_transport_t* transport;
    int count;
} flush_ctx_t;

static int flush_to_file_cb(const elog_msg_header_t* hdr,
                              const char* tag, const char* msg, void* user) {
    flush_ctx_t* ctx = (flush_ctx_t*)user;
    if (!ctx->transport || !ctx->transport->base.is_open(&ctx->transport->base)) {
        return 0;
    }

    elog_format_ctx_t fmt;
    elog_format_text(&fmt, hdr, tag, msg);

    if (fmt.len > 0) {
        ctx->transport->base.write(&ctx->transport->base,
                                    (const uint8_t*)fmt.buf, (size_t)fmt.len);
    }
    ctx->count++;
    return 0;
}

/* entry 总大小 (4B prefix + aligned entry_len) */
static size_t entry_total_size(uint32_t entry_len) {
    return 4 + ((entry_len + 3) & ~3U);
}

/* 读取 entry_len 前缀 (处理环形边界) */
static uint32_t ring_read_u32(const uint8_t* buf, size_t cap, size_t pos) {
    uint32_t val;
    if (pos + 4 <= cap) {
        memcpy(&val, buf + pos, 4);
    } else {
        uint8_t tmp[4];
        size_t first = cap - pos;
        memcpy(tmp, buf + pos, first);
        memcpy(tmp + first, buf, 4 - first);
        memcpy(&val, tmp, 4);
    }
    return val;
}

/* 手动推进 read_pos 跳过已消费的条目 */
static size_t advance_read_pos(const uint8_t* buf, size_t cap,
                                size_t from, size_t to) {
    size_t pos = from;
    int skipped = 0;
    while (pos != to && skipped < 4096) {
        uint32_t entry_len = ring_read_u32(buf, cap, pos);
        if (entry_len == 0) break;
        pos = (pos + entry_total_size(entry_len)) % cap;
        skipped++;
    }
    return pos;
}

void* elogd_flusher_thread(void* arg) {
    (void)arg;
    elog_file_transport_t file_transport;
    elog_file_transport_init(&file_transport, ELOG_DAEMON_LOG_FILE, 0, 0);
    file_transport.base.open(&file_transport.base);

    /* 从 tail 开始 (不重复已有日志) */
    size_t my_read_pos = 0;
    if (g_daemon_rb) {
        elog_mutex_lock(&g_daemon_rb->lock);
        my_read_pos = g_daemon_rb->write_pos;
        elog_mutex_unlock(&g_daemon_rb->lock);
    }

    while (g_daemon_running) {
        if (!g_daemon_rb) {
            usleep(100000);
            continue;
        }

        elog_mutex_lock(&g_daemon_rb->lock);

        /* 等待新数据 */
        while (my_read_pos == g_daemon_rb->write_pos && g_daemon_running) {
            elog_cond_timedwait(&g_daemon_rb->not_empty, &g_daemon_rb->lock, 1000);
        }

        if (my_read_pos != g_daemon_rb->write_pos) {
            flush_ctx_t ctx = { .transport = &file_transport, .count = 0 };
            size_t end = g_daemon_rb->write_pos;

            elog_ring_buf_flush_range(g_daemon_rb, my_read_pos, end,
                                       flush_to_file_cb, &ctx);

            /* 推进 read_pos */
            my_read_pos = advance_read_pos(g_daemon_rb->buffer,
                                            g_daemon_rb->buf_capacity,
                                            my_read_pos, end);
            file_transport.base.flush(&file_transport.base);
        }

        elog_mutex_unlock(&g_daemon_rb->lock);
    }

    /* 退出前 flush 剩余数据 */
    if (g_daemon_rb) {
        elog_mutex_lock(&g_daemon_rb->lock);
        if (my_read_pos != g_daemon_rb->write_pos) {
            flush_ctx_t ctx = { .transport = &file_transport, .count = 0 };
            elog_ring_buf_flush_range(g_daemon_rb, my_read_pos,
                                       g_daemon_rb->write_pos,
                                       flush_to_file_cb, &ctx);
            file_transport.base.flush(&file_transport.base);
        }
        elog_mutex_unlock(&g_daemon_rb->lock);
    }

    file_transport.base.close(&file_transport.base);
    elog_file_transport_deinit(&file_transport);
    return NULL;
}

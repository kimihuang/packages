/**
 * @file elogd_flush.c
 * @brief Flusher — 遍历所有 log buffer, 格式化后写入各自独立文件
 */

#include "elogd.h"
#include "elog_buf.h"
#include "elog_transport_file.h"
#include "elog_format.h"
#include "elog_event.h"
#include "elog_def.h"
#include "elog_debug.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

extern volatile bool g_daemon_running;

/* 每 buffer 独立的 flush 状态 */
typedef struct {
    elog_file_transport_t* transport;
    size_t read_pos;
} buf_flush_t;

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

/* EVENTS buffer 的格式化回调 — 解码 TLV 并输出 */
static int flush_event_to_file_cb(const elog_msg_header_t* hdr,
                                   const char* tag, const char* msg, void* user) {
    flush_ctx_t* ctx = (flush_ctx_t*)user;
    if (!ctx->transport || !ctx->transport->base.is_open(&ctx->transport->base)) return 0;

    uint32_t event_id = 0;
    if (hdr->msg_len >= 4) memcpy(&event_id, msg, 4);

    elog_format_ctx_t fmt;
    elog_format_timestamp(hdr->timestamp, fmt.buf, sizeof(fmt.buf));
    size_t pos = strlen(fmt.buf);

    int n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos,
                     " I %-5u %-5u %s: EVENT(%u) [",
                     (unsigned)hdr->pid, (unsigned)hdr->tid,
                     tag ? tag : "", (unsigned)event_id);
    if (n > 0 && pos + (size_t)n < sizeof(fmt.buf)) pos += (size_t)n;

    /* 跳过 event_id, 定位 TLV payload */
    const uint8_t* payload = (const uint8_t*)msg + 4;
    size_t payload_len = hdr->msg_len - 4;
    if (payload_len > 0 && payload[0] == ELOG_EVENT_TYPE_LIST) {
        payload += 2; payload_len -= 2;
    }

    elog_event_parser_t parser;
    elog_event_parser_init(&parser, payload, payload_len);

    bool first = true;
    elog_event_value_t val;
    while (elog_event_parser_next(&parser, &val) == ELOG_OK) {
        if (!first) {
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, ", ");
            if (n > 0 && pos + (size_t)n < sizeof(fmt.buf)) pos += (size_t)n;
        }
        first = false;
        switch (val.type) {
        case ELOG_EVENT_TYPE_INT32:
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "%d", val.int32_val); break;
        case ELOG_EVENT_TYPE_INT64:
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "%lld", (long long)val.int64_val); break;
        case ELOG_EVENT_TYPE_FLOAT:
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "%.6g", (double)val.float_val); break;
        case ELOG_EVENT_TYPE_STRING:
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "\"%.*s\"",
                         (int)val.str_len, val.str_val); break;
        case ELOG_EVENT_TYPE_LIST:
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "[...%u]", val.list_count); break;
        default:
            n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "?"); break;
        }
        if (n > 0 && pos + (size_t)n < sizeof(fmt.buf)) pos += (size_t)n;
    }

    n = snprintf(fmt.buf + pos, sizeof(fmt.buf) - pos, "]\n");
    if (n > 0 && pos + (size_t)n < sizeof(fmt.buf)) pos += (size_t)n;

    ctx->transport->base.write(&ctx->transport->base, (const uint8_t*)fmt.buf, pos);
    ctx->count++;
    return 0;
}

static size_t entry_total_size(uint32_t entry_len) {
    return 4 + ((entry_len + 3) & ~3U);
}

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

    /* 每 buffer 独立文件 */
    static const char* k_log_files[ELOG_ID_MAX] = {
        "/var/log/elog_main.log",
        "/var/log/elog_radio.log",
        "/var/log/elog_events.log",
        "/var/log/elog_system.log",
        "/var/log/elog_crash.log",
        "/var/log/elog_kernel.log",
    };

    elog_file_transport_t transports[ELOG_ID_MAX];
    buf_flush_t buf_states[ELOG_ID_MAX];

    for (int i = 0; i < ELOG_ID_MAX; i++) {
        elog_file_transport_init(&transports[i], k_log_files[i], 0, 0);
        transports[i].base.open(&transports[i].base);

        buf_states[i].transport = &transports[i];
        buf_states[i].read_pos = 0;

        elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)i);
        if (rb) {
            elog_mutex_lock(&rb->lock);
            buf_states[i].read_pos = rb->write_pos;  /* 从 tail 开始 */
            elog_mutex_unlock(&rb->lock);
        }
    }

    while (g_daemon_running) {
        for (int bid = 0; bid < ELOG_ID_MAX; bid++) {
            elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)bid);
            if (!rb) continue;

            elog_mutex_lock(&rb->lock);

            if (buf_states[bid].read_pos != rb->write_pos) {
                flush_ctx_t ctx = { .transport = &transports[bid], .count = 0 };
                size_t end = rb->write_pos;

                ELOG_DBG_FLUSHER("flush[%d]: rp=%zu wp=%zu", bid, buf_states[bid].read_pos, rb->write_pos);

                int (*cb)(const elog_msg_header_t*, const char*, const char*, void*) =
                    (bid == ELOG_ID_EVENTS) ? flush_event_to_file_cb : flush_to_file_cb;

                elog_ring_buf_flush_range(rb, buf_states[bid].read_pos, end,
                                           cb, &ctx);

                size_t new_rp = advance_read_pos(
                    rb->buffer, rb->buf_capacity,
                    buf_states[bid].read_pos, end);

                ELOG_DBG_FLUSHER("flushed[%d]: count=%d new_rp=%zu", bid, ctx.count, new_rp);

                buf_states[bid].read_pos = new_rp;
                transports[bid].base.flush(&transports[bid].base);
            }

            elog_mutex_unlock(&rb->lock);
        }

        usleep(500000);  /* 500ms 轮询间隔 */
    }

    /* 退出前 flush 剩余数据 */
    for (int bid = 0; bid < ELOG_ID_MAX; bid++) {
        elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)bid);
        if (!rb) continue;

        elog_mutex_lock(&rb->lock);
        if (buf_states[bid].read_pos != rb->write_pos) {
            flush_ctx_t ctx = { .transport = &transports[bid], .count = 0 };
            int (*cb)(const elog_msg_header_t*, const char*, const char*, void*) =
                (bid == ELOG_ID_EVENTS) ? flush_event_to_file_cb : flush_to_file_cb;
            elog_ring_buf_flush_range(rb, buf_states[bid].read_pos,
                                       rb->write_pos,
                                       cb, &ctx);
            transports[bid].base.flush(&transports[bid].base);
        }
        elog_mutex_unlock(&rb->lock);

        transports[bid].base.close(&transports[bid].base);
        elog_file_transport_deinit(&transports[bid]);
    }

    return NULL;
}

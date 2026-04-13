/**
 * @file elogd_flush.c
 * @brief Flusher — 遍历所有 log buffer, 格式化后写入各自独立文件
 */

#include "elogd.h"
#include "elog_buf.h"
#include "elog_transport_file.h"
#include "elog_format.h"
#include "elog_def.h"
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

                elog_ring_buf_flush_range(rb, buf_states[bid].read_pos, end,
                                           flush_to_file_cb, &ctx);

                buf_states[bid].read_pos = advance_read_pos(
                    rb->buffer, rb->buf_capacity,
                    buf_states[bid].read_pos, end);

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
            elog_ring_buf_flush_range(rb, buf_states[bid].read_pos,
                                       rb->write_pos,
                                       flush_to_file_cb, &ctx);
            transports[bid].base.flush(&transports[bid].base);
        }
        elog_mutex_unlock(&rb->lock);

        transports[bid].base.close(&transports[bid].base);
        elog_file_transport_deinit(&transports[bid]);
    }

    return NULL;
}

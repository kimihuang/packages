/**
 * @file elog_reader.c
 * @brief 日志读取器实现
 *
 * 阻塞读取直接使用 rb->lock + rb->not_empty，无需额外 condvar。
 */

#include "elog_reader.h"
#include "elog_buf.h"
#include "elog_filter.h"
#include "elog_format.h"
#include "elog_port.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===== 内部辅助 ===== */

static uint32_t ring_read_u32_local(const uint8_t* buf, size_t cap, size_t pos) {
    if (!buf || cap < 4) return 0;
    uint8_t b[4];
    size_t first = cap - pos;
    if (first >= 4) {
        memcpy(b, buf + pos, 4);
    } else {
        memcpy(b, buf + pos, first);
        memcpy(b + first, buf, 4 - first);
    }
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline size_t entry_total_local(uint32_t entry_len) {
    return 4 + ((entry_len + 3) & ~3U);
}

/* ===== ReaderList ===== */

void elog_reader_list_init(elog_reader_list_t* list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
    elog_mutex_init(&list->lock);
}

void elog_reader_list_destroy(elog_reader_list_t* list) {
    if (!list) return;
    list->count = 0;
    elog_mutex_destroy(&list->lock);
}

int elog_reader_list_add(elog_reader_list_t* list, elog_reader_t* reader) {
    if (!list || !reader) return ELOG_ERR_PARAM;
    if (list->count >= ELOG_MAX_READERS) return ELOG_ERR_FULL;

    elog_mutex_lock(&list->lock);
    list->readers[list->count++] = reader;
    elog_mutex_unlock(&list->lock);
    return ELOG_OK;
}

int elog_reader_list_remove(elog_reader_list_t* list, elog_reader_t* reader) {
    if (!list || !reader) return ELOG_ERR_PARAM;

    elog_mutex_lock(&list->lock);
    for (uint8_t i = 0; i < list->count; i++) {
        if (list->readers[i] == reader) {
            for (uint8_t j = i; j < list->count - 1; j++) {
                list->readers[j] = list->readers[j + 1];
            }
            list->count--;
            elog_mutex_unlock(&list->lock);
            return ELOG_OK;
        }
    }
    elog_mutex_unlock(&list->lock);
    return ELOG_ERR_PARAM;
}

/* ===== Reader ===== */

elog_reader_t* elog_reader_create(uint8_t mode, uint16_t pid_filter) {
    elog_reader_t* r = (elog_reader_t*)calloc(1, sizeof(*r));
    if (!r) return NULL;

    r->mode = mode;
    r->min_level = ELOG_LEVEL_VERBOSE;
    r->pid_filter = pid_filter;
    return r;
}

void elog_reader_attach(elog_reader_t* reader, void* ring_buf) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)ring_buf;
    if (!reader || !rb) return;
    reader->read_pos = rb->write_pos;
}

void elog_reader_destroy(elog_reader_t* reader) {
    free(reader);
}

/* flush 回调: 文本格式化 + 过滤 */
typedef struct {
    char*       buf;
    size_t      len;
    size_t      pos;
    int         count;
    elog_level_t min_level;
    uint16_t    pid_filter;
} reader_ctx_t;

static int reader_callback(const elog_msg_header_t* hdr,
                           const char* tag, const char* msg, void* user) {
    reader_ctx_t* ctx = (reader_ctx_t*)user;

    if ((elog_level_t)hdr->level < ctx->min_level) return 0;
    if (ctx->pid_filter != 0 && hdr->pid != ctx->pid_filter) return 0;

    if (ctx->pos < ctx->len - 1) {
        int n = snprintf(ctx->buf + ctx->pos, ctx->len - ctx->pos,
                         "%c %s: %s\n",
                         elog_level_str((elog_level_t)hdr->level),
                         tag, msg);
        if (n > 0) {
            ctx->pos += (size_t)n;
            if (ctx->pos >= ctx->len) ctx->pos = ctx->len - 1;
        }
    }

    ctx->count++;
    return 0;
}

int elog_reader_read(elog_reader_t* reader, void* ring_buf,
                     char* buf, size_t len, int timeout_ms) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)ring_buf;
    if (!reader || !rb || !buf || len == 0) return ELOG_ERR_PARAM;

    reader_ctx_t ctx = {
        .buf = buf,
        .len = len,
        .pos = 0,
        .count = 0,
        .min_level = reader->min_level,
        .pid_filter = reader->pid_filter,
    };

    elog_mutex_lock(&rb->lock);

    /* 无新数据 */
    while (reader->read_pos == rb->write_pos) {
        if (reader->mode & ELOG_READ_NONBLOCK) {
            elog_mutex_unlock(&rb->lock);
            buf[0] = '\0';
            return 0;
        }

        /* 阻塞等待: 使用 rb->not_empty (写入时已 signal) */
        int wait_ret = elog_cond_timedwait(&rb->not_empty, &rb->lock, timeout_ms);
        if (wait_ret != ELOG_OK) {
            elog_mutex_unlock(&rb->lock);
            buf[0] = '\0';
            return 0;
        }
    }

    /* 有新数据: flush_range */
    size_t end = rb->write_pos;
    int flushed = elog_ring_buf_flush_range(rb, reader->read_pos, end,
                                            reader_callback, &ctx);
    (void)flushed;

    /* 推进 reader->read_pos */
    size_t pos = reader->read_pos;
    int skipped = 0;
    while (pos != end && skipped < 4096) {
        uint32_t entry_len = ring_read_u32_local(rb->buffer, rb->buf_capacity, pos);
        if (entry_len == 0) break;
        pos = (pos + entry_total_local(entry_len)) % rb->buf_capacity;
        skipped++;
    }
    reader->read_pos = pos;

    elog_mutex_unlock(&rb->lock);

    buf[ctx.pos] = '\0';
    return ctx.count;
}

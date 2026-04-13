/**
 * @file elog_buf_ring.c
 * @brief RingLogBuffer 实现
 *
 * RingBuffer 中的日志条目布局:
 *   [uint32_t entry_len] [elog_msg_header_t(16B)] [tag] [msg]
 *
 * entry_len = sizeof(header) + tag_len + msg_len
 * 总占用 = 4 + entry_len
 *
 * 环形遍历:
 *   next_pos = (pos + 4 + entry_len) % buf_capacity
 */

#include "elog_buf.h"
#include "elog_stats.h"
#include "elog_prune.h"
#include <stdlib.h>
#include <string.h>

/* 条目前缀长度 */
#define ENTRY_PREFIX_LEN  sizeof(uint32_t)

/* 对齐到 4 字节 */
#define ALIGN4(x) (((x) + 3) & ~3U)

/* 获取条目总占用 */
static inline size_t entry_total_size(uint32_t entry_len) {
    return ENTRY_PREFIX_LEN + ALIGN4(entry_len);
}

/* 从环形 buffer 中读取数据到线性 dst（自动处理跨边界） */
static void ring_read(const uint8_t* buf, size_t cap, size_t pos,
                      void* dst, size_t len) {
    if (len == 0) return;
    size_t first = cap - pos;
    if (first >= len) {
        memcpy(dst, buf + pos, len);
    } else {
        memcpy(dst, buf + pos, first);
        memcpy((uint8_t*)dst + first, buf, len - first);
    }
}

/* 从环形 buffer 读取 uint32_t */
static uint32_t ring_read_u32(const uint8_t* buf, size_t cap, size_t pos) {
    uint32_t val;
    ring_read(buf, cap, pos, &val, sizeof(val));
    return val;
}

/* 将线性 src 数据写入环形 buffer（自动处理跨边界） */
static void ring_write(uint8_t* buf, size_t cap, size_t pos,
                       const void* src, size_t len) {
    if (len == 0) return;
    size_t first = cap - pos;
    if (first >= len) {
        memcpy(buf + pos, src, len);
    } else {
        memcpy(buf + pos, src, first);
        memcpy(buf, (const uint8_t*)src + first, len - first);
    }
}

/* gettid 已由 elog_port_gettid() 替代，此处不再需要本地定义 */

/* ---- 内部函数 ---- */

/* 写入条目到 buffer (不加锁版本) */
static int ring_write_unlocked(elog_ring_buf_t* rb, elog_id_t log_id, elog_level_t level,
                                uint16_t line, const char* tag, const char* msg,
                                uint16_t explicit_msg_len, /* 0 = use strlen */
                                bool force, bool use_provided_ids,
                                uint16_t prov_pid, uint16_t prov_tid) {
    uint16_t tag_len = tag ? (uint16_t)ELOG_MIN(strlen(tag), ELOG_MAX_TAG_LEN - 1) : 0;
    uint16_t msg_len;
    if (explicit_msg_len > 0) {
        msg_len = (uint16_t)ELOG_MIN(explicit_msg_len, ELOG_MAX_MSG_LEN - 1);
    } else {
        msg_len = msg ? (uint16_t)ELOG_MIN(strlen(msg), ELOG_MAX_MSG_LEN - 1) : 0;
    }

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = (uint8_t)log_id;
    hdr.level = (uint8_t)level;
    hdr.timestamp = (uint32_t)elog_port_now();
    hdr.pid = use_provided_ids ? prov_pid : elog_port_getpid();
    hdr.tid = use_provided_ids ? prov_tid : elog_port_gettid();
    hdr.line = line;
    hdr.tag_len = tag_len;
    hdr.msg_len = msg_len;

    uint32_t entry_len = sizeof(hdr) + tag_len + msg_len;
    size_t total = entry_total_size(entry_len);

    /* ---- ISR 临界区开始 ---- */
    /* 保护: overwrite 循环 + buffer 写入 + pos/count 更新
     * bare-metal: 关中断; Linux: no-op (单线程无竞态) */
    elog_isr_state_t isr_state = elog_port_isr_save();

    /* 检查空间 */
    /* 计算已用空间 (用于 prune 判断) */
    size_t used_space;
    if (rb->write_pos >= rb->read_pos) {
        used_space = rb->write_pos - rb->read_pos;
    } else {
        used_space = rb->buf_capacity - rb->read_pos + rb->write_pos;
    }

    if (rb->write_pos >= rb->read_pos) {
        size_t free_space = rb->buf_capacity - rb->write_pos + rb->read_pos;
        if (free_space < total) {
            if (!force && !rb->overwrite) {
                return ELOG_ERR_FULL;
            }
            if (!force && rb->prune && tag &&
                elog_prune_is_low_priority(rb->prune, tag) &&
                elog_prune_should_prune(rb->prune, (uint32_t)used_space,
                                        (uint32_t)rb->buf_capacity)) {
                return ELOG_ERR_PRUNED;
            }
            /* 覆写: 推进 read_pos 直到有足够空间 */
            while ((rb->buf_capacity - rb->write_pos + rb->read_pos) < total && rb->count > 0) {
                uint32_t old_len = ring_read_u32(rb->buffer, rb->buf_capacity, rb->read_pos);
                rb->read_pos = (rb->read_pos + entry_total_size(old_len)) % rb->buf_capacity;
                rb->count--;
            }
        }
    } else {
        size_t free_space = rb->read_pos - rb->write_pos;
        if (free_space < total) {
            if (!force && !rb->overwrite) {
                return ELOG_ERR_FULL;
            }
            if (!force && rb->prune && tag &&
                elog_prune_is_low_priority(rb->prune, tag) &&
                elog_prune_should_prune(rb->prune, (uint32_t)used_space,
                                        (uint32_t)rb->buf_capacity)) {
                return ELOG_ERR_PRUNED;
            }
            while ((rb->read_pos - rb->write_pos) < total && rb->count > 0) {
                uint32_t old_len = ring_read_u32(rb->buffer, rb->buf_capacity, rb->read_pos);
                rb->read_pos = (rb->read_pos + entry_total_size(old_len)) % rb->buf_capacity;
                rb->count--;
            }
        }
    }

    /* 写入 entry_len (4 bytes, buf_capacity >= 256, write_pos 处 4 字节必连续) */
    memcpy(rb->buffer + rb->write_pos, &entry_len, sizeof(entry_len));
    size_t pos = rb->write_pos + ENTRY_PREFIX_LEN;

    /* 写入 header + tag + msg（可能跨环形边界） */
    ring_write(rb->buffer, rb->buf_capacity, pos, &hdr, sizeof(hdr));
    if (tag_len > 0) {
        ring_write(rb->buffer, rb->buf_capacity,
                   (pos + sizeof(hdr)) % rb->buf_capacity, tag, tag_len);
    }
    if (msg_len > 0) {
        ring_write(rb->buffer, rb->buf_capacity,
                   (pos + sizeof(hdr) + tag_len) % rb->buf_capacity, msg, msg_len);
    }

    rb->write_pos = (rb->write_pos + total) % rb->buf_capacity;
    rb->count++;

    elog_port_isr_restore(isr_state);
    /* ---- ISR 临界区结束 ---- */

    return ELOG_OK;
}

/* ---- vtable 实现 ---- */

int elog_ring_buf_log(elog_buf_t* self, elog_id_t log_id, elog_level_t level,
                       uint16_t pid, uint16_t tid, uint16_t line,
                       const char* tag, const char* msg) {
    (void)pid; (void)tid; /* 由内部获取 */
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb || !rb->buffer) return ELOG_ERR_NOT_INIT;

    elog_mutex_lock(&rb->lock);
    int ret = ring_write_unlocked(rb, log_id, level, line, tag, msg, 0, false, false, 0, 0);
    if (ret == ELOG_OK) {
        elog_cond_signal(&rb->not_empty);
    }
    elog_mutex_unlock(&rb->lock);
    return ret;
}

int elog_ring_buf_log_from(elog_buf_t* self, elog_id_t log_id, elog_level_t level,
                            uint16_t pid, uint16_t tid, uint16_t line,
                            const char* tag, const char* msg) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb || !rb->buffer) return ELOG_ERR_NOT_INIT;

    elog_mutex_lock(&rb->lock);
    int ret = ring_write_unlocked(rb, log_id, level, line, tag, msg, 0, false, true, pid, tid);
    if (ret == ELOG_OK) {
        elog_cond_signal(&rb->not_empty);
    }
    elog_mutex_unlock(&rb->lock);
    return ret;
}

/* Binary-safe write: msg 可以包含 NUL 字节, 使用 explicit msg_len */
int elog_ring_buf_log_from_binary(elog_buf_t* self, elog_id_t log_id, elog_level_t level,
                                   uint16_t pid, uint16_t tid, uint16_t line,
                                   const char* tag, const uint8_t* msg, uint16_t msg_len) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb || !rb->buffer) return ELOG_ERR_NOT_INIT;

    elog_mutex_lock(&rb->lock);
    int ret = ring_write_unlocked(rb, log_id, level, line, tag, (const char*)msg,
                                   msg_len, false, true, pid, tid);
    if (ret == ELOG_OK) {
        elog_cond_signal(&rb->not_empty);
    }
    elog_mutex_unlock(&rb->lock);
    return ret;
}

/* 内部: 从 [from, end) 范围 flush，返回 (flushed_count, new_pos) */
static int flush_range_unlocked(elog_ring_buf_t* rb, size_t from, size_t end,
                                 int (*callback)(const elog_msg_header_t*,
                                                 const char*, const char*, void*),
                                 void* user, size_t* out_pos) {
    int flushed = 0;
    size_t pos = from;

    while (pos != end && flushed < 4096) {
        uint32_t entry_len = ring_read_u32(rb->buffer, rb->buf_capacity, pos);

        elog_msg_header_t hdr;
        size_t data_pos = (pos + ENTRY_PREFIX_LEN) % rb->buf_capacity;
        ring_read(rb->buffer, rb->buf_capacity, data_pos, &hdr, sizeof(hdr));

        char tag_buf[ELOG_MAX_TAG_LEN];
        tag_buf[0] = '\0';
        if (hdr.tag_len > 0 && hdr.tag_len < ELOG_MAX_TAG_LEN) {
            size_t tag_pos = (data_pos + sizeof(hdr)) % rb->buf_capacity;
            ring_read(rb->buffer, rb->buf_capacity, tag_pos, tag_buf, hdr.tag_len);
            tag_buf[hdr.tag_len] = '\0';
        }

        char msg_buf[ELOG_MAX_MSG_LEN];
        msg_buf[0] = '\0';
        if (hdr.msg_len > 0 && hdr.msg_len < ELOG_MAX_MSG_LEN) {
            size_t msg_pos = (data_pos + sizeof(hdr) + hdr.tag_len) % rb->buf_capacity;
            ring_read(rb->buffer, rb->buf_capacity, msg_pos, msg_buf, hdr.msg_len);
            msg_buf[hdr.msg_len] = '\0';
        }

        int ret = callback(&hdr, tag_buf, msg_buf, user);
        if (ret < 0) break;

        pos = (pos + entry_total_size(entry_len)) % rb->buf_capacity;
        flushed++;
    }

    if (out_pos) *out_pos = pos;
    return flushed;
}

int elog_ring_buf_flush(elog_buf_t* self,
                         int (*callback)(const elog_msg_header_t* hdr,
                                         const char* tag, const char* msg, void* user),
                         void* user) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb || !rb->buffer || !callback) return ELOG_ERR_PARAM;

    elog_mutex_lock(&rb->lock);
    size_t new_pos;
    int flushed = flush_range_unlocked(rb, rb->read_pos, rb->write_pos, callback, user, &new_pos);
    rb->read_pos = new_pos;
    elog_mutex_unlock(&rb->lock);
    return flushed;
}

int elog_ring_buf_flush_range(elog_ring_buf_t* rb, size_t from, size_t to,
                               int (*callback)(const elog_msg_header_t*,
                                               const char*, const char*, void*),
                               void* user) {
    if (!rb || !rb->buffer || !callback) return ELOG_ERR_PARAM;
    /* 调用者需持有 rb->lock */
    size_t new_pos;
    return flush_range_unlocked(rb, from, to, callback, user, &new_pos);
}

void elog_ring_buf_clear(elog_buf_t* self) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb) return;
    elog_mutex_lock(&rb->lock);
    rb->write_pos = 0;
    rb->read_pos = 0;
    rb->count = 0;
    memset(rb->buffer, 0, rb->buf_capacity);
    elog_mutex_unlock(&rb->lock);
}

size_t elog_ring_buf_size(elog_buf_t* self) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb) return 0;
    elog_mutex_lock(&rb->lock);
    /* 计算已用空间 */
    size_t used;
    if (rb->write_pos >= rb->read_pos) {
        used = rb->write_pos - rb->read_pos;
    } else {
        used = rb->buf_capacity - rb->read_pos + rb->write_pos;
    }
    elog_mutex_unlock(&rb->lock);
    return used;
}

size_t elog_ring_buf_capacity(elog_buf_t* self) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    return rb ? rb->buf_capacity : 0;
}

bool elog_ring_buf_is_empty(elog_buf_t* self) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)self;
    if (!rb) return true;
    elog_mutex_lock(&rb->lock);
    bool empty = (rb->count == 0);
    elog_mutex_unlock(&rb->lock);
    return empty;
}

/* ---- 初始化/销毁 ---- */

int elog_ring_buf_init(elog_ring_buf_t* rb, size_t capacity) {
    if (!rb || capacity < 256) return ELOG_ERR_PARAM;

    memset(rb, 0, sizeof(*rb));
    rb->buffer = (uint8_t*)calloc(1, capacity);
    if (!rb->buffer) return ELOG_ERR_NOMEM;

    rb->buf_capacity = capacity;
    rb->overwrite = true;
    elog_mutex_init(&rb->lock);
    elog_cond_init(&rb->not_empty);

    /* 设置 vtable */
    rb->base.log      = elog_ring_buf_log;
    rb->base.flush    = elog_ring_buf_flush;
    rb->base.clear    = elog_ring_buf_clear;
    rb->base.size     = elog_ring_buf_size;
    rb->base.capacity = elog_ring_buf_capacity;
    rb->base.is_empty = elog_ring_buf_is_empty;

    return ELOG_OK;
}

int elog_ring_buf_init_static(elog_ring_buf_t* rb, uint8_t* buf, size_t capacity) {
    if (!rb || !buf || capacity < 256) return ELOG_ERR_PARAM;

    memset(rb, 0, sizeof(*rb));
    rb->buffer = buf;
    rb->buf_capacity = capacity;
    rb->overwrite = true;
    elog_mutex_init(&rb->lock);
    elog_cond_init(&rb->not_empty);

    rb->base.log      = elog_ring_buf_log;
    rb->base.flush    = elog_ring_buf_flush;
    rb->base.clear    = elog_ring_buf_clear;
    rb->base.size     = elog_ring_buf_size;
    rb->base.capacity = elog_ring_buf_capacity;
    rb->base.is_empty = elog_ring_buf_is_empty;

    return ELOG_OK;
}

void elog_ring_buf_destroy(elog_ring_buf_t* rb) {
    if (!rb) return;
    elog_mutex_destroy(&rb->lock);
    elog_cond_destroy(&rb->not_empty);
    free(rb->buffer);
    rb->buffer = NULL;
    rb->buf_capacity = 0;
    rb->write_pos = 0;
    rb->read_pos = 0;
    rb->count = 0;
    rb->prune = NULL;
}

void elog_ring_buf_set_prune(elog_ring_buf_t* rb, struct elog_prune* prune) {
    if (rb) rb->prune = prune;
}

/* ISR 安全版本 */
int elog_ring_buf_log_isr(elog_ring_buf_t* rb, elog_id_t log_id, elog_level_t level,
                           uint16_t pid, uint16_t tid, uint16_t line,
                           const char* tag, const char* msg) {
    if (!rb || !rb->buffer) return ELOG_ERR_NOT_INIT;
    /* ISR 中不加锁，直接写入，强制覆写 */
    return ring_write_unlocked(rb, log_id, level, line, tag, msg, 0, true, false, 0, 0);
}

/**
 * @file elog_buf.h
 * @brief LogBuffer 抽象接口 + RingLogBuffer 实现
 *
 * RingBuffer 中的日志条目布局:
 *   [uint32_t entry_len] [elog_msg_header_t] [tag (tag_len)] [msg (msg_len)]
 *
 * entry_len = sizeof(header) + tag_len + msg_len (不含自身 4B)
 * 遍历时: next_pos = current_pos + 4 + entry_len
 */

#ifndef ELOG_BUF_H
#define ELOG_BUF_H

#include "elog_def.h"
#include "elog_filter.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== LogBuffer 抽象接口 (C 函数指针 vtable) ===== */
typedef struct elog_buf {
    int  (*log)(struct elog_buf* self, elog_id_t log_id, elog_level_t level,
                uint16_t pid, uint16_t tid, uint16_t line,
                const char* tag, const char* msg);

    int  (*flush)(struct elog_buf* self, int (*callback)(const elog_msg_header_t* hdr,
                  const char* tag, const char* msg, void* user), void* user);

    void (*clear)(struct elog_buf* self);
    size_t (*size)(struct elog_buf* self);
    size_t (*capacity)(struct elog_buf* self);
    bool  (*is_empty)(struct elog_buf* self);
} elog_buf_t;

/* ===== RingLogBuffer ===== */

typedef struct {
    elog_buf_t base;                    /* vtable (必须在第一个字段) */

    uint8_t*  buffer;                   /* 数据区域 */
    size_t    buf_capacity;             /* 总容量 */
    size_t    write_pos;                /* 写入位置 */
    size_t    read_pos;                 /* 读取位置 (flush 跟踪) */
    size_t    count;                    /* 当前条目数 */
    bool      overwrite;                /* true: 覆写旧日志 */

    /* Linux pthread 同步 */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
} elog_ring_buf_t;

/**
 * 初始化 RingLogBuffer (使用动态分配)
 * @param rb RingLogBuffer 实例
 * @param capacity 缓冲区大小 (bytes)
 * @return ELOG_OK 或错误码
 */
int elog_ring_buf_init(elog_ring_buf_t* rb, size_t capacity);

/**
 * 使用静态 buffer 初始化
 * @param rb RingLogBuffer 实例
 * @param buf 用户提供的缓冲区
 * @param capacity 缓冲区大小
 * @param buf_is_static true: buf 为静态/栈内存, 不需要 free
 * @return ELOG_OK 或错误码
 */
int elog_ring_buf_init_static(elog_ring_buf_t* rb, uint8_t* buf, size_t capacity);

/**
 * 销毁 RingLogBuffer (释放动态分配的内存)
 */
void elog_ring_buf_destroy(elog_ring_buf_t* rb);

/* RingLogBuffer vtable 方法 */
int  elog_ring_buf_log(elog_buf_t* self, elog_id_t log_id, elog_level_t level,
                       uint16_t pid, uint16_t tid, uint16_t line,
                       const char* tag, const char* msg);
int  elog_ring_buf_flush(elog_buf_t* self,
                         int (*callback)(const elog_msg_header_t* hdr,
                                         const char* tag, const char* msg, void* user),
                         void* user);
void elog_ring_buf_clear(elog_buf_t* self);
size_t elog_ring_buf_size(elog_buf_t* self);
size_t elog_ring_buf_capacity(elog_buf_t* self);
bool  elog_ring_buf_is_empty(elog_buf_t* self);

/* ISR 安全版本 (不加锁, 强制覆写) */
int  elog_ring_buf_log_isr(elog_ring_buf_t* rb, elog_id_t log_id, elog_level_t level,
                           uint16_t pid, uint16_t tid, uint16_t line,
                           const char* tag, const char* msg);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_BUF_H */

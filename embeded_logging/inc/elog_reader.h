/**
 * @file elog_reader.h
 * @brief 日志读取器 — 独立读取位置，支持阻塞/非阻塞模式和超时
 *
 * 多个 reader 各自维护 read_pos，互不干扰。
 * 阻塞读取复用 RingBuffer 的 not_empty condvar。
 */

#ifndef ELOG_READER_H
#define ELOG_READER_H

#include "elog_def.h"
#include "elog_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== elog_reader_t ===== */

typedef struct elog_reader {
    size_t          read_pos;       /* 本地读取位置 */
    uint8_t         mode;           /* ELOG_READ_NONBLOCK | ELOG_READ_BINARY */
    elog_level_t    min_level;      /* 最低日志级别过滤 */
    uint16_t        pid_filter;     /* PID 过滤 (0=不过滤) */
} elog_reader_t;

/* ===== elog_reader_list_t ===== */

typedef struct {
    elog_reader_t* readers[ELOG_MAX_READERS];
    uint8_t        count;
    elog_mutex_t   lock;
} elog_reader_list_t;

void elog_reader_list_init(elog_reader_list_t* list);
void elog_reader_list_destroy(elog_reader_list_t* list);
int  elog_reader_list_add(elog_reader_list_t* list, elog_reader_t* reader);
int  elog_reader_list_remove(elog_reader_list_t* list, elog_reader_t* reader);

elog_reader_t* elog_reader_create(uint8_t mode, uint16_t pid_filter);
void elog_reader_attach(elog_reader_t* reader, void* ring_buf);
void elog_reader_destroy(elog_reader_t* reader);

/**
 * 读取日志
 * 阻塞模式下复用 ring buffer 的 not_empty condvar 等待新数据。
 */
int elog_reader_read(elog_reader_t* reader, void* ring_buf,
                     char* buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_READER_H */

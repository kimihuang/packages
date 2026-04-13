/**
 * @file elog_debug.h
 * @brief Compile-time debug macros - per-category, zero overhead when disabled
 *
 * 使用方法:
 *   cmake -DCMAKE_C_FLAGS="-DELOG_DBG_RING -DELOG_DBG_EVENT"
 *   或启用全部:
 *   cmake -DCMAKE_C_FLAGS="-DELOG_DBG_ALL"
 *
 * 类别:
 *   ELOG_DBG_RING     - ring buffer: write/overwrite/prune/flush
 *   ELOG_DBG_EVENT    - event encode: add/list/parser/submit
 *   ELOG_DBG_LISTENER - daemon listener: recv/route/write error
 *   ELOG_DBG_FLUSHER  - daemon flusher: flush range/advance read_pos
 *   ELOG_DBG_READER   - daemon reader: client/push thread
 *   ELOG_DBG_CLIENT   - client: connect/send/reconnect
 *   ELOG_DBG_PRUNE    - prune: usage/threshold decision
 *   ELOG_DBG_ALL      - enable all
 */

#ifndef ELOG_DEBUG_H
#define ELOG_DEBUG_H

#include <stdio.h>

/* ===== RING: ring buffer write/overwrite/prune/flush ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_RING)
#define ELOG_DBG_RING(fmt, ...) \
    fprintf(stderr, "[RING] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_RING(fmt, ...) ((void)0)
#endif

/* ===== EVENT: event encode/parse/submit ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_EVENT)
#define ELOG_DBG_EVENT(fmt, ...) \
    fprintf(stderr, "[EVENT] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_EVENT(fmt, ...) ((void)0)
#endif

/* ===== LISTENER: daemon listener ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_LISTENER)
#define ELOG_DBG_LISTENER(fmt, ...) \
    fprintf(stderr, "[LISTEN] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_LISTENER(fmt, ...) ((void)0)
#endif

/* ===== FLUSHER: daemon flusher ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_FLUSHER)
#define ELOG_DBG_FLUSHER(fmt, ...) \
    fprintf(stderr, "[FLUSH] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_FLUSHER(fmt, ...) ((void)0)
#endif

/* ===== READER: daemon push thread ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_READER)
#define ELOG_DBG_READER(fmt, ...) \
    fprintf(stderr, "[READER] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_READER(fmt, ...) ((void)0)
#endif

/* ===== CLIENT: client send ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_CLIENT)
#define ELOG_DBG_CLIENT(fmt, ...) \
    fprintf(stderr, "[CLIENT] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_CLIENT(fmt, ...) ((void)0)
#endif

/* ===== PRUNE: prune strategy ===== */

#if defined(ELOG_DBG_ALL) || defined(ELOG_DBG_PRUNE)
#define ELOG_DBG_PRUNE(fmt, ...) \
    fprintf(stderr, "[PRUNE] " fmt "\n", ##__VA_ARGS__)
#else
#define ELOG_DBG_PRUNE(fmt, ...) ((void)0)
#endif

#endif /* ELOG_DEBUG_H */

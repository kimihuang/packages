/**
 * @file elog_def.h
 * @brief elog 内部类型定义（枚举、结构体、辅助宏）
 *
 * 此头文件被所有内部模块和公共 API 依赖，定义了日志系统的核心数据结构。
 */

#ifndef ELOG_DEF_H
#define ELOG_DEF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include "elog_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 日志级别 ===== */
typedef enum {
    ELOG_LEVEL_VERBOSE = 0,   /* 详细调试信息 */
    ELOG_LEVEL_DEBUG   = 1,   /* 调试信息 */
    ELOG_LEVEL_INFO    = 2,   /* 一般信息 */
    ELOG_LEVEL_WARN    = 3,   /* 警告 */
    ELOG_LEVEL_ERROR   = 4,   /* 错误 */
    ELOG_LEVEL_FATAL   = 5,   /* 致命错误 */
    ELOG_LEVEL_NONE    = 6,   /* 关闭所有日志 */
} elog_level_t;

/* ===== 日志 ID（缓冲区类型） ===== */
typedef enum {
    ELOG_ID_MAIN    = 0,    /* 主日志缓冲区 */
    ELOG_ID_RADIO   = 1,    /* 无线电/通信模块 */
    ELOG_ID_EVENTS  = 2,    /* 事件日志 (二进制) */
    ELOG_ID_SYSTEM  = 3,    /* 系统日志 */
    ELOG_ID_CRASH   = 4,    /* 崩溃日志 */
    ELOG_ID_KERNEL  = 5,    /* 内核日志 */
    ELOG_ID_MAX
} elog_id_t;

/* ===== 日志条目头（packed 结构体） ===== */
/*
 * 线路上完整消息布局:
 *   [elog_msg_header_t] [tag (tag_len bytes)] [msg (msg_len bytes)]
 *
 * sizeof(header) = 16 bytes (ARM32), 缓存行对齐友好。
 * 借鉴 Android LogBufferElement 的 packed 设计，减少内存占用。
 */
#pragma pack(push, 1)
typedef struct {
    uint8_t  log_id;          /* ELOG_ID_MAIN 等 */
    uint8_t  level;           /* ELOG_LEVEL_INFO 等 */
    uint32_t timestamp;       /* Unix epoch seconds */
    uint16_t pid;             /* 进程 ID */
    uint16_t tid;             /* 线程 ID */
    uint16_t line;            /* 源码行号 (0 = 未知) */
    uint16_t tag_len;         /* tag 长度 (不含 NUL) */
    uint16_t msg_len;         /* 消息长度 (不含 NUL) */
} elog_msg_header_t;
#pragma pack(pop)

_Static_assert(sizeof(elog_msg_header_t) == 16, "elog_msg_header_t must be 16 bytes");

/* ===== 单条日志在 RingBuffer 中的存储 ===== */
/*
 * RingBuffer 中每条日志的布局:
 *   [uint32_t total_len] [elog_msg_header_t] [tag] [msg]
 *
 * total_len = sizeof(header) + tag_len + msg_len (不含自身 4B)
 * 用于遍历时跳转到下一条。
 */

/* ===== 级别字符串映射 ===== */
static inline const char* elog_level_str(elog_level_t level) {
    switch (level) {
        case ELOG_LEVEL_VERBOSE: return "V";
        case ELOG_LEVEL_DEBUG:   return "D";
        case ELOG_LEVEL_INFO:    return "I";
        case ELOG_LEVEL_WARN:    return "W";
        case ELOG_LEVEL_ERROR:   return "E";
        case ELOG_LEVEL_FATAL:   return "F";
        default:                 return "?";
    }
}

/* ===== ANSI 颜色码 ===== */
#define ELOG_COLOR_NONE   ""
#define ELOG_COLOR_RED    "\033[0;31m"
#define ELOG_COLOR_GREEN  "\033[0;32m"
#define ELOG_COLOR_YELLOW "\033[0;33m"
#define ELOG_COLOR_BLUE   "\033[0;34m"
#define ELOG_COLOR_MAGENTA "\033[0;35m"
#define ELOG_COLOR_CYAN   "\033[0;36m"
#define ELOG_COLOR_WHITE  "\033[0;37m"
#define ELOG_COLOR_RESET  "\033[0m"

static inline const char* elog_level_color(elog_level_t level) {
#if ELOG_COLOR_ENABLE
    switch (level) {
        case ELOG_LEVEL_VERBOSE: return ELOG_COLOR_WHITE;
        case ELOG_LEVEL_DEBUG:   return ELOG_COLOR_CYAN;
        case ELOG_LEVEL_INFO:    return ELOG_COLOR_GREEN;
        case ELOG_LEVEL_WARN:    return ELOG_COLOR_YELLOW;
        case ELOG_LEVEL_ERROR:   return ELOG_COLOR_RED;
        case ELOG_LEVEL_FATAL:   return ELOG_COLOR_MAGENTA;
        default:                 return ELOG_COLOR_NONE;
    }
#else
    (void)level;
    return ELOG_COLOR_NONE;
#endif
}

/* ===== Event 日志 TLV 类型 ===== */
typedef enum {
    ELOG_EVENT_TYPE_INT32  = 0,   /* 1B type + 4B value */
    ELOG_EVENT_TYPE_INT64  = 1,   /* 1B type + 8B value */
    ELOG_EVENT_TYPE_STRING = 2,   /* 1B type + 4B len + N bytes */
    ELOG_EVENT_TYPE_FLOAT  = 3,   /* 1B type + 4B value */
    ELOG_EVENT_TYPE_LIST   = 4,   /* 1B type + 1B count + elements */
} elog_event_type_t;

/* ===== 读取模式标志 ===== */
#define ELOG_READ_NONBLOCK  0x01
#define ELOG_READ_BINARY    0x04

/* ===== 错误码 ===== */
#define ELOG_OK             0
#define ELOG_ERR_PARAM     -1
#define ELOG_ERR_NOMEM     -2
#define ELOG_ERR_FULL      -3
#define ELOG_ERR_NOT_INIT  -4
#define ELOG_ERR_OVERFLOW  -5
#define ELOG_ERR_BUSY      -6

/* ===== 内部辅助宏 ===== */
#define ELOG_MIN(a, b)      ((a) < (b) ? (a) : (b))
#define ELOG_MAX(a, b)      ((a) > (b) ? (a) : (b))
#define ELOG_ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define ELOG_UNUSED(x)      ((void)(x))

#ifdef __cplusplus
}
#endif

#endif /* ELOG_DEF_H */

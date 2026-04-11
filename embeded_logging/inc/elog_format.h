/**
 * @file elog_format.h
 * @brief 日志格式化器 — 文本格式化 (logcat 风格) 和二进制格式化
 */

#ifndef ELOG_FORMAT_H
#define ELOG_FORMAT_H

#include "elog_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 格式化上下文 */
typedef struct {
    char  buf[ELOG_MAX_FORMAT_LEN];
    int   pos;
    int   len;    /* 已写入长度 */
} elog_format_ctx_t;

/**
 * 文本格式化 (logcat 风格)
 *
 * 输出格式:
 *   [颜色] MM-DD HH:MM:SS L  PID TID tag: message [颜色重置]
 *
 * 或不带时间戳:
 *   [颜色] L/tag: message [颜色重置]
 */
int elog_format_text(elog_format_ctx_t* ctx, const elog_msg_header_t* hdr,
                     const char* tag, const char* msg);

/**
 * 二进制格式化 (紧凑格式)
 *
 * 输出: [elog_msg_header_t] [tag] [msg]
 * 用于网络传输/文件存储，无格式化开销。
 */
int elog_format_binary(elog_format_ctx_t* ctx, const elog_msg_header_t* hdr,
                       const char* tag, const char* msg);

/**
 * 格式化时间戳: "MM-DD HH:MM:SS"
 * @param ts Unix epoch seconds
 * @param buf 输出缓冲区 (至少 18 bytes)
 * @return 写入长度
 */
int elog_format_timestamp(uint32_t ts, char* buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_FORMAT_H */

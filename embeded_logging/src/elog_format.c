/**
 * @file elog_format.c
 * @brief 日志格式化器实现
 */

#include "elog_format.h"
#include "elog_filter.h"
#include "elog_port.h"
#include <stdio.h>
#include <string.h>

int elog_format_timestamp(uint32_t ts, char* buf, size_t buf_len) {
    if (!buf || buf_len < 18) return 0;

    int hour, minute, second, day, month;
    elog_port_localtime(ts, &hour, &minute, &second, &day, &month);

    int n = snprintf(buf, buf_len, "%02d-%02d %02d:%02d:%02d",
                     month, day, hour, minute, second);
    return (n > 0 && (size_t)n < buf_len) ? n : 0;
}

int elog_format_text(elog_format_ctx_t* ctx, const elog_msg_header_t* hdr,
                     const char* tag, const char* msg) {
    if (!ctx || !hdr) return ELOG_ERR_PARAM;

    ctx->pos = 0;
    ctx->len = 0;
    char* buf = ctx->buf;
    size_t cap = sizeof(ctx->buf);
    int n;

    const char* color = elog_level_color((elog_level_t)hdr->level);
    const char* level_ch = elog_level_str((elog_level_t)hdr->level);

    /* 时间戳 */
    if (hdr->log_id == ELOG_ID_MAIN || hdr->log_id == ELOG_ID_SYSTEM) {
        /* 假设 header 的 timestamp_enabled 与全局配置一致 */
        char ts_buf[18];
        int ts_len = elog_format_timestamp(hdr->timestamp, ts_buf, sizeof(ts_buf));
        if (ts_len > 0) {
            n = snprintf(buf + ctx->pos, cap - ctx->pos, "%s ", ts_buf);
            if (n > 0) { ctx->pos += n; }
        }
    }

    /* 级别字符 + PID + TID + tag */
    n = snprintf(buf + ctx->pos, cap - ctx->pos,
                 "%s%-1s %-5u %-5u %s: ",
                 color, level_ch,
                 (unsigned)hdr->pid, (unsigned)hdr->tid,
                 tag ? tag : "");
    if (n > 0 && (size_t)(ctx->pos + n) < cap) {
        ctx->pos += n;
    }

    /* 消息体 */
    if (msg) {
        size_t remain = cap - ctx->pos;
        n = snprintf(buf + ctx->pos, remain, "%s", msg);
        if (n > 0 && (size_t)(ctx->pos + n) < cap) {
            ctx->pos += n;
        }
    }

    /* 颜色重置 */
    const char* reset = elog_level_color((elog_level_t)hdr->level);
    if (reset[0] != '\0') {
        n = snprintf(buf + ctx->pos, cap - ctx->pos, "%s", ELOG_COLOR_RESET);
        if (n > 0 && (size_t)(ctx->pos + n) < cap) {
            ctx->pos += n;
        }
    }

    /* 换行 */
    if (ctx->pos > 0 && (size_t)ctx->pos < cap - 1) {
        buf[ctx->pos++] = '\n';
    }
    buf[ctx->pos] = '\0';
    ctx->len = ctx->pos;

    return ctx->len;
}

int elog_format_binary(elog_format_ctx_t* ctx, const elog_msg_header_t* hdr,
                       const char* tag, const char* msg) {
    if (!ctx || !hdr) return ELOG_ERR_PARAM;

    size_t total = sizeof(elog_msg_header_t) + hdr->tag_len + hdr->msg_len;
    if (total > sizeof(ctx->buf)) return ELOG_ERR_FULL;

    ctx->pos = 0;
    memcpy(ctx->buf, hdr, sizeof(elog_msg_header_t));
    ctx->pos = sizeof(elog_msg_header_t);

    if (tag && hdr->tag_len > 0) {
        memcpy(ctx->buf + ctx->pos, tag, hdr->tag_len);
        ctx->pos += hdr->tag_len;
    }

    if (msg && hdr->msg_len > 0) {
        memcpy(ctx->buf + ctx->pos, msg, hdr->msg_len);
        ctx->pos += hdr->msg_len;
    }

    ctx->len = ctx->pos;
    return ctx->len;
}

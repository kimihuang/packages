/**
 * @file elog_event.c
 * @brief Event 日志实现 — TLV 编码、嵌套列表、解析、提交
 *
 * 存储布局:
 *   [uint32_t event_id] [uint8_t type=LIST] [uint8_t count] [elements...]
 *
 * 每个元素:
 *   INT32:  [type=0] [int32_t value]           = 5 bytes
 *   INT64:  [type=1] [int64_t value]           = 9 bytes
 *   STRING: [type=2] [uint32_t len] [char[]]    = 5+N bytes
 *   LIST:   [type=3] [uint8_t count] [elems...] = 2+N bytes
 *   FLOAT:  [type=4] [float value]             = 5 bytes
 */

#include "elog_event.h"
#include "elog_config.h"
#include "elog_port.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "elog_debug.h"

#define EVENT_HDR_SIZE  (sizeof(uint32_t) + 2)  /* event_id + type + count */

struct elog_event_ctx {
    uint32_t event_id;
    uint8_t  storage[ELOG_EVENT_STORAGE_SIZE];
    unsigned pos;
    unsigned count;              /* 当前层元素计数 */
    bool     overflow;
    unsigned len;

    /* 嵌套列表支持 */
    unsigned list_depth;
    unsigned count_stack[ELOG_EVENT_LIST_DEPTH]; /* 每层元素计数 */
    unsigned pos_stack[ELOG_EVENT_LIST_DEPTH];   /* 每层 count 回填位置 */
};

elog_event_ctx_t* elog_event_create(uint32_t event_id) {
    elog_event_ctx_t* ctx = (elog_event_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->event_id = event_id;
    ctx->pos = 0;
    ctx->count = 0;
    ctx->overflow = false;
    ctx->len = 0;
    ctx->list_depth = 0;

    /* 写入 event_id (4 bytes) */
    memcpy(ctx->storage, &event_id, sizeof(event_id));
    ctx->pos = sizeof(event_id);

    /* 写入 LIST type (1 byte) */
    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_LIST;

    /* 占位 count (1 byte), 提交时回填 */
    ctx->storage[ctx->pos++] = 0;

    return ctx;
}

void elog_event_destroy(elog_event_ctx_t* ctx) {
    free(ctx);
}

static int ensure_space(elog_event_ctx_t* ctx, size_t needed) {
    if (!ctx || ctx->overflow) return ELOG_ERR_OVERFLOW;
    if (ctx->pos + needed > ELOG_EVENT_STORAGE_SIZE) {
        ELOG_DBG_EVENT("overflow: pos=%u needed=%zu storage=%d", ctx->pos, needed, ELOG_EVENT_STORAGE_SIZE);
        ctx->overflow = true;
        return ELOG_ERR_OVERFLOW;
    }
    return ELOG_OK;
}

/* 递增当前层的元素计数 */
static void inc_count(elog_event_ctx_t* ctx) {
    if (ctx->list_depth > 0) {
        ctx->count_stack[ctx->list_depth - 1]++;
    } else {
        ctx->count++;
    }
}

int elog_event_add_int32(elog_event_ctx_t* ctx, int32_t value) {
    int ret = ensure_space(ctx, 1 + sizeof(int32_t));
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_INT32;
    memcpy(ctx->storage + ctx->pos, &value, sizeof(value));
    ctx->pos += sizeof(value);
    inc_count(ctx);
    ELOG_DBG_EVENT("add int32=%d (pos=%u)", value, ctx->pos);
    return ELOG_OK;
}

int elog_event_add_int64(elog_event_ctx_t* ctx, int64_t value) {
    int ret = ensure_space(ctx, 1 + sizeof(int64_t));
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_INT64;
    memcpy(ctx->storage + ctx->pos, &value, sizeof(value));
    ctx->pos += sizeof(value);
    inc_count(ctx);
    ELOG_DBG_EVENT("add int64=%lld (pos=%u)", (long long)value, ctx->pos);
    return ELOG_OK;
}

int elog_event_add_float(elog_event_ctx_t* ctx, float value) {
    int ret = ensure_space(ctx, 1 + sizeof(float));
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_FLOAT;
    memcpy(ctx->storage + ctx->pos, &value, sizeof(value));
    ctx->pos += sizeof(value);
    inc_count(ctx);
    ELOG_DBG_EVENT("add float=%.4g (pos=%u)", (double)value, ctx->pos);
    return ELOG_OK;
}

int elog_event_add_string(elog_event_ctx_t* ctx, const char* str) {
    if (!str) return ELOG_ERR_PARAM;
    uint32_t slen = (uint32_t)strlen(str);

    int ret = ensure_space(ctx, 1 + sizeof(uint32_t) + slen);
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_STRING;
    memcpy(ctx->storage + ctx->pos, &slen, sizeof(slen));
    ctx->pos += sizeof(slen);
    memcpy(ctx->storage + ctx->pos, str, slen);
    ctx->pos += slen;
    inc_count(ctx);
    ELOG_DBG_EVENT("add string=\"%.*s\" len=%u (pos=%u)", (int)ELOG_MIN(slen,20), str, slen, ctx->pos);
    return ELOG_OK;
}

/* ===== 嵌套列表 ===== */

int elog_event_list_begin(elog_event_ctx_t* ctx) {
    if (!ctx || ctx->overflow) return ELOG_ERR_OVERFLOW;
    if (ctx->list_depth >= ELOG_EVENT_LIST_DEPTH) return ELOG_ERR_OVERFLOW;

    int ret = ensure_space(ctx, 2);
    if (ret != ELOG_OK) return ret;

    /* 递增父级计数: 嵌套 LIST 算作父级的一个元素 */
    if (ctx->list_depth > 0) {
        ctx->count_stack[ctx->list_depth - 1]++;
    } else {
        ctx->count++;
    }

    /* 设置当前嵌套层的 count 追踪 */
    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_LIST;
    ctx->pos_stack[ctx->list_depth] = ctx->pos; /* 指向 count 字节 (type 之后) */
    ctx->count_stack[ctx->list_depth] = 0;
    ctx->storage[ctx->pos++] = 0; /* count 占位 */

    ctx->list_depth++;
    ELOG_DBG_EVENT("list_begin: depth=%u (pos=%u)", ctx->list_depth, ctx->pos);
    return ELOG_OK;
}

int elog_event_list_end(elog_event_ctx_t* ctx) {
    if (!ctx || ctx->list_depth == 0) return ELOG_ERR_PARAM;

    ctx->list_depth--;
    ELOG_DBG_EVENT("list_end: depth=%u count=%u", ctx->list_depth, ctx->count_stack[ctx->list_depth]);

    /* 回填 count */
    ctx->storage[ctx->pos_stack[ctx->list_depth]] =
        (uint8_t)ctx->count_stack[ctx->list_depth];

    return ELOG_OK;
}

/* ===== 获取编码数据 ===== */

const uint8_t* elog_event_data(const elog_event_ctx_t* ctx, size_t* len) {
    if (!ctx) {
        if (len) *len = 0;
        return NULL;
    }

    /* 回填所有未关闭的 LIST count */
    elog_event_ctx_t* m = (elog_event_ctx_t*)ctx;
    for (unsigned d = ctx->list_depth; d > 0; d--) {
        m->storage[m->pos_stack[d - 1]] = (uint8_t)m->count_stack[d - 1];
    }
    if (ctx->list_depth > 0) {
        ELOG_DBG_EVENT("auto-close %u unclosed lists", ctx->list_depth);
    }
    /* 回填顶层 count */
    m->storage[sizeof(uint32_t) + 1] = (uint8_t)ctx->count;

    /* 单元素时剥离外层 LIST (借鉴 Android 优化) */
    if (ctx->count <= 1) {
        if (len) *len = ctx->pos - EVENT_HDR_SIZE;
        return ctx->storage + EVENT_HDR_SIZE;
    }

    if (len) *len = ctx->pos;
    return ctx->storage;
}

uint32_t elog_event_id(const elog_event_ctx_t* ctx) {
    return ctx ? ctx->event_id : 0;
}

/* ===== 提交到 daemon (需要 daemon/elogd.h) ===== */

#ifdef ELOG_EVENT_HAS_DAEMON
#include "elogd.h"

int elog_event_submit(elog_event_ctx_t* ctx, const char* tag) {
    if (!ctx) return ELOG_ERR_PARAM;
    if (!tag) tag = "";

    size_t data_len;
    const uint8_t* data = elog_event_data(ctx, &data_len);
    if (!data || data_len == 0) {
        elog_event_destroy(ctx);
        return ELOG_ERR_OVERFLOW;
    }

    ELOG_DBG_EVENT("submit: id=%u tag=%s len=%zu", ctx->event_id, tag, data_len);

    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_EVENTS;
    hdr.level = ELOG_LEVEL_INFO;
    hdr.timestamp = (uint32_t)elog_port_now();
    hdr.pid = elog_port_getpid();
    hdr.tid = elog_port_gettid();
    hdr.tag_len = (uint16_t)ELOG_MIN(strlen(tag), ELOG_MAX_TAG_LEN - 1);
    hdr.msg_len = (uint16_t)ELOG_MIN(data_len, ELOG_MAX_MSG_LEN - 1);

    int ret = elogd_client_send_binary(&hdr, tag, data, hdr.msg_len);
    elog_event_destroy(ctx);
    return ret;
}
#endif /* ELOG_EVENT_HAS_DAEMON */

/* ===== 解析器 ===== */

void elog_event_parser_init(elog_event_parser_t* parser,
                             const uint8_t* data, size_t len) {
    if (!parser) return;
    parser->data = data;
    parser->data_len = len;
    parser->pos = 0;
}

int elog_event_parser_next(elog_event_parser_t* parser, elog_event_value_t* out) {
    if (!parser || !out) return ELOG_ERR_PARAM;
    if (parser->pos + 1 > parser->data_len) return ELOG_ERR_PARAM;

    uint8_t type = parser->data[parser->pos++];

    switch (type) {
    case ELOG_EVENT_TYPE_INT32:
        if (parser->pos + sizeof(int32_t) > parser->data_len) return ELOG_ERR_PARAM;
        memcpy(&out->int32_val, parser->data + parser->pos, sizeof(int32_t));
        parser->pos += sizeof(int32_t);
        out->type = ELOG_EVENT_TYPE_INT32;
        ELOG_DBG_EVENT("parse int32=%d", out->int32_val);
        return ELOG_OK;

    case ELOG_EVENT_TYPE_INT64:
        if (parser->pos + sizeof(int64_t) > parser->data_len) return ELOG_ERR_PARAM;
        memcpy(&out->int64_val, parser->data + parser->pos, sizeof(int64_t));
        parser->pos += sizeof(int64_t);
        out->type = ELOG_EVENT_TYPE_INT64;
        ELOG_DBG_EVENT("parse int64=%lld", (long long)out->int64_val);
        return ELOG_OK;

    case ELOG_EVENT_TYPE_FLOAT:
        if (parser->pos + sizeof(float) > parser->data_len) return ELOG_ERR_PARAM;
        memcpy(&out->float_val, parser->data + parser->pos, sizeof(float));
        parser->pos += sizeof(float);
        out->type = ELOG_EVENT_TYPE_FLOAT;
        ELOG_DBG_EVENT("parse float=%.4g", (double)out->float_val);
        return ELOG_OK;

    case ELOG_EVENT_TYPE_STRING: {
        if (parser->pos + sizeof(uint32_t) > parser->data_len) return ELOG_ERR_PARAM;
        uint32_t slen;
        memcpy(&slen, parser->data + parser->pos, sizeof(uint32_t));
        parser->pos += sizeof(uint32_t);
        if (parser->pos + slen > parser->data_len) return ELOG_ERR_PARAM;
        out->str_val = (const char*)(parser->data + parser->pos);
        out->str_len = slen;
        parser->pos += slen;
        out->type = ELOG_EVENT_TYPE_STRING;
        ELOG_DBG_EVENT("parse string=\"%.*s\" len=%u", (int)ELOG_MIN(out->str_len,20), out->str_val, out->str_len);
        return ELOG_OK;
    }

    case ELOG_EVENT_TYPE_LIST:
        if (parser->pos + 1 > parser->data_len) return ELOG_ERR_PARAM;
        out->list_count = parser->data[parser->pos++];
        out->type = ELOG_EVENT_TYPE_LIST;
        ELOG_DBG_EVENT("parse list count=%u", out->list_count);
        return ELOG_OK;

    default:
        return ELOG_ERR_PARAM;
    }
}

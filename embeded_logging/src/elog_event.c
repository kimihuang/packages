/**
 * @file elog_event.c
 * @brief Event 日志实现 — TLV 二进制编码
 *
 * 存储布局:
 *   [uint32_t event_id] [uint8_t type=LIST] [uint8_t count] [elements...]
 *
 * 每个元素:
 *   INT32:  [type=0] [int32_t value]           = 5 bytes
 *   INT64:  [type=1] [int64_t value]           = 9 bytes
 *   STRING: [type=2] [uint32_t len] [char[]]    = 5+N bytes
 *   FLOAT:  [type=3] [float value]             = 5 bytes
 */

#include "elog_event.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define EVENT_HDR_SIZE  (sizeof(uint32_t) + 2)  /* event_id + type + count */

struct elog_event_ctx {
    uint32_t event_id;
    uint8_t  storage[ELOG_EVENT_STORAGE_SIZE];
    unsigned pos;                /* 当前写入位置 */
    unsigned count;              /* 顶层元素计数 */
    bool     overflow;           /* 缓冲区溢出 */
    unsigned len;                /* 编码后的总长度 */
};

elog_event_ctx_t* elog_event_create(uint32_t event_id) {
    elog_event_ctx_t* ctx = (elog_event_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->event_id = event_id;
    ctx->pos = 0;
    ctx->count = 0;
    ctx->overflow = false;
    ctx->len = 0;

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
        ctx->overflow = true;
        return ELOG_ERR_OVERFLOW;
    }
    return ELOG_OK;
}

int elog_event_add_int32(elog_event_ctx_t* ctx, int32_t value) {
    int ret = ensure_space(ctx, 1 + sizeof(int32_t));
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_INT32;
    memcpy(ctx->storage + ctx->pos, &value, sizeof(value));
    ctx->pos += sizeof(value);
    ctx->count++;
    return ELOG_OK;
}

int elog_event_add_int64(elog_event_ctx_t* ctx, int64_t value) {
    int ret = ensure_space(ctx, 1 + sizeof(int64_t));
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_INT64;
    memcpy(ctx->storage + ctx->pos, &value, sizeof(value));
    ctx->pos += sizeof(value);
    ctx->count++;
    return ELOG_OK;
}

int elog_event_add_float(elog_event_ctx_t* ctx, float value) {
    int ret = ensure_space(ctx, 1 + sizeof(float));
    if (ret != ELOG_OK) return ret;

    ctx->storage[ctx->pos++] = (uint8_t)ELOG_EVENT_TYPE_FLOAT;
    memcpy(ctx->storage + ctx->pos, &value, sizeof(value));
    ctx->pos += sizeof(value);
    ctx->count++;
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
    ctx->count++;
    return ELOG_OK;
}

const uint8_t* elog_event_data(const elog_event_ctx_t* ctx, size_t* len) {
    if (!ctx) {
        if (len) *len = 0;
        return NULL;
    }

    /* 回填 count */
    /* 注意: 这里 cast 掉 const，因为 data() 可能在 submit 之前被调用 */
    elog_event_ctx_t* mutable_ctx = (elog_event_ctx_t*)ctx;
    mutable_ctx->storage[sizeof(uint32_t) + 1] = (uint8_t)ctx->count;

    /* 单元素时剥离外层 LIST (借鉴 Android 优化) */
    if (ctx->count <= 1) {
        /* 跳过 event_id(4) + type(1) + count(1) = 6 bytes,
           返回第一个元素的原始数据 */
        if (len) *len = ctx->pos - EVENT_HDR_SIZE;
        return ctx->storage + EVENT_HDR_SIZE;
    }

    if (len) *len = ctx->pos;
    return ctx->storage;
}

uint32_t elog_event_id(const elog_event_ctx_t* ctx) {
    return ctx ? ctx->event_id : 0;
}

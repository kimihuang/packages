/**
 * @file elog_event.h
 * @brief Event 日志 — TLV 二进制编码、解析、提交
 *
 * 借鉴 Android android_log_event_list 的链式 API 和 TLV 编码格式。
 * 内置缓冲区，无需动态内存分配。
 *
 * 使用示例:
 *   elog_event_ctx_t* e = elog_event_create(1001);
 *   elog_event_add_int32(e, sensor_id);
 *   elog_event_add_float(e, temperature);
 *   elog_event_add_string(e, "celsius");
 *   elog_event_submit(e, "sensor");
 *
 * 嵌套列表:
 *   elog_event_list_begin(e);
 *   elog_event_add_int32(e, x);
 *   elog_event_add_int32(e, y);
 *   elog_event_list_end(e);
 *
 * 解析:
 *   elog_event_parser_t parser;
 *   elog_event_parser_init(&parser, data, len);
 *   elog_event_value_t val;
 *   while (elog_event_parser_next(&parser, &val) == ELOG_OK) { ... }
 */

#ifndef ELOG_EVENT_H
#define ELOG_EVENT_H

#include "elog_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最大列表嵌套深度 */
#define ELOG_EVENT_LIST_DEPTH  ELOG_EVENT_MAX_DEPTH

/* ===== 编码器 ===== */

/* Event 编码上下文 (不透明) */
typedef struct elog_event_ctx elog_event_ctx_t;

elog_event_ctx_t* elog_event_create(uint32_t event_id);
void elog_event_destroy(elog_event_ctx_t* ctx);

int elog_event_add_int32(elog_event_ctx_t* ctx, int32_t value);
int elog_event_add_int64(elog_event_ctx_t* ctx, int64_t value);
int elog_event_add_float(elog_event_ctx_t* ctx, float value);
int elog_event_add_string(elog_event_ctx_t* ctx, const char* str);

/* 嵌套列表 (最多 ELOG_EVENT_LIST_DEPTH 层) */
int elog_event_list_begin(elog_event_ctx_t* ctx);
int elog_event_list_end(elog_event_ctx_t* ctx);

/* 获取编码后的数据指针和长度 */
const uint8_t* elog_event_data(const elog_event_ctx_t* ctx, size_t* len);
uint32_t elog_event_id(const elog_event_ctx_t* ctx);

/* 提交到 daemon EVENTS buffer (ctx 被销毁) */
int elog_event_submit(elog_event_ctx_t* ctx, const char* tag);

/* ===== 解析器 ===== */

/** 解析出的值 */
typedef struct {
    elog_event_type_t type;
    union {
        int32_t  int32_val;
        int64_t  int64_val;
        float    float_val;
        struct {
            const char* str_val;
            uint32_t    str_len;
        };
        struct {
            uint8_t list_count;
        };
    };
} elog_event_value_t;

/** 解析器上下文 */
typedef struct {
    const uint8_t* data;
    size_t         data_len;
    size_t         pos;
} elog_event_parser_t;

void elog_event_parser_init(elog_event_parser_t* parser,
                             const uint8_t* data, size_t len);
int elog_event_parser_next(elog_event_parser_t* parser, elog_event_value_t* out);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_EVENT_H */

/**
 * @file elog_event.h
 * @brief Event 日志 — TLV 二进制编码
 *
 * 借鉴 Android android_log_event_list 的链式 API 和 TLV 编码格式。
 * 内置缓冲区，无需动态内存分配。
 *
 * 使用示例:
 *   elog_event_ctx_t* e = elog_event_create(1001);
 *   elog_event_add_int32(e, sensor_id);
 *   elog_event_add_float(e, temperature);
 *   elog_event_add_string(e, "celsius");
 *   elog_event_submit(e);
 *   elog_event_destroy(e);
 */

#ifndef ELOG_EVENT_H
#define ELOG_EVENT_H

#include "elog_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最大列表嵌套深度 */
#define ELOG_EVENT_LIST_DEPTH  ELOG_EVENT_MAX_DEPTH

/* Event 编码上下文 (不透明) */
typedef struct elog_event_ctx elog_event_ctx_t;

/**
 * 创建 Event 上下文
 * @param event_id 事件 ID
 * @return 上下文指针, 或 NULL (内存不足)
 */
elog_event_ctx_t* elog_event_create(uint32_t event_id);

/**
 * 销毁 Event 上下文
 */
void elog_event_destroy(elog_event_ctx_t* ctx);

/**
 * 添加 int32 参数
 */
int elog_event_add_int32(elog_event_ctx_t* ctx, int32_t value);

/**
 * 添加 int64 参数
 */
int elog_event_add_int64(elog_event_ctx_t* ctx, int64_t value);

/**
 * 添加 float 参数
 */
int elog_event_add_float(elog_event_ctx_t* ctx, float value);

/**
 * 添加 string 参数
 */
int elog_event_add_string(elog_event_ctx_t* ctx, const char* str);

/**
 * 获取编码后的数据指针和长度
 */
const uint8_t* elog_event_data(const elog_event_ctx_t* ctx, size_t* len);

/**
 * 获取 Event ID
 */
uint32_t elog_event_id(const elog_event_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_EVENT_H */

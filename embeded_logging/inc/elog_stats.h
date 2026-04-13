/**
 * @file elog_stats.h
 * @brief 日志统计 — 轻量计数器
 *
 * 借鉴 Android LogStatistics 的多维统计，精简为纯计数器数组，
 * 适用于 RAM 有限的嵌入式环境。
 */

#ifndef ELOG_STATS_H
#define ELOG_STATS_H

#include "elog_def.h"
#include "elog_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t total_count[ELOG_ID_MAX];      /* 每个缓冲区总写入数 */
    uint32_t dropped_count[ELOG_ID_MAX];    /* 每个缓冲区丢弃数 */
    uint32_t level_count[6];                /* 按级别统计 (V/D/I/W/E/F) */
    uint32_t buffer_usage;                  /* 当前缓冲区使用量 (bytes) */
    uint32_t buffer_peak;                   /* 缓冲区使用峰值 (bytes) */
} elog_stats_t;

/**
 * 初始化统计
 */
void elog_stats_init(elog_stats_t* s);

/**
 * 记录一次成功写入
 */
void elog_stats_on_log(elog_stats_t* s, uint8_t log_id, uint8_t level);

/**
 * 记录一次成功写入 (ISR-safe, 使用 atomic_inc)
 */
void elog_stats_on_log_isr(elog_stats_t* s, uint8_t log_id, uint8_t level);

/**
 * 记录一次丢弃
 */
void elog_stats_on_drop(elog_stats_t* s, uint8_t log_id);

/**
 * 更新缓冲区使用量
 */
void elog_stats_update_usage(elog_stats_t* s, uint32_t usage);

/**
 * 获取统计摘要字符串
 * @return 写入的字符数
 */
int elog_stats_get(const elog_stats_t* s, char* buf, size_t len);

/**
 * 重置统计
 */
void elog_stats_reset(elog_stats_t* s);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_STATS_H */

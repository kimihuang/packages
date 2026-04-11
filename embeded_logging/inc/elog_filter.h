/**
 * @file elog_filter.h
 * @brief 日志过滤器 — 全局级别 + 标签级别过滤
 */

#ifndef ELOG_FILTER_H
#define ELOG_FILTER_H

#include "elog_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 标签级别映射 */
typedef struct {
    const char* tag;
    uint8_t     level;     /* 该标签允许的最低级别 */
} elog_tag_level_t;

typedef struct {
    uint8_t           global_level;                    /* 全局最低级别 */
    elog_tag_level_t  tag_levels[ELOG_MAX_TAGS];       /* 标签级别映射 */
    uint8_t           tag_count;                       /* 已注册标签数 */
    bool              color_enabled;                   /* 彩色输出 */
    bool              timestamp_enabled;               /* 时间戳 */
    bool              source_location;                 /* 源码位置 */
} elog_filter_t;

/**
 * 初始化过滤器
 */
void elog_filter_init(elog_filter_t* f);

/**
 * 判断是否应该记录此日志
 * @return true: 记录, false: 过滤掉
 */
bool elog_filter_check(const elog_filter_t* f, uint8_t level, const char* tag);

/**
 * 设置全局最低级别
 */
void elog_filter_set_global_level(elog_filter_t* f, uint8_t level);

/**
 * 获取全局最低级别
 */
uint8_t elog_filter_get_global_level(const elog_filter_t* f);

/**
 * 设置标签级别 (覆盖全局级别)
 * @return ELOG_OK 或 ELOG_ERR_FULL
 */
int elog_filter_set_tag_level(elog_filter_t* f, const char* tag, uint8_t level);

/**
 * 重置标签级别 (恢复使用全局级别)
 * @return ELOG_OK 或 ELOG_ERR_PARAM
 */
int elog_filter_reset_tag_level(elog_filter_t* f, const char* tag);

/**
 * 获取标签级别 (未设置则返回 global_level)
 */
uint8_t elog_filter_get_tag_level(const elog_filter_t* f, const char* tag);

/**
 * 重置所有标签级别
 */
void elog_filter_reset_all(elog_filter_t* f);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_FILTER_H */

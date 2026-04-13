/**
 * @file elog_prune.h
 * @brief 优先级裁剪 — 保护高优先级日志，优先丢弃低优先级日志
 *
 * 借鉴 Android PruneList 的 "~" / "~!" 语法:
 *   ~tag    — 保护 tag，最后裁剪
 *   tag     — 低优先级，优先裁剪
 */

#ifndef ELOG_PRUNE_H
#define ELOG_PRUNE_H

#include "elog_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 裁剪规则 */
typedef struct {
    char     tag[ELOG_MAX_TAG_LEN]; /* 标签名 */
    bool     negated;                /* true: 保护 (~前缀); false: 低优先级 */
} elog_prune_rule_t;

typedef struct elog_prune {
    elog_prune_rule_t high_rules[ELOG_MAX_PRUNE_RULES];
    uint8_t          high_count;
    elog_prune_rule_t low_rules[ELOG_MAX_PRUNE_RULES];
    uint8_t          low_count;
    uint32_t         threshold_pct;
} elog_prune_t;

/**
 * 初始化裁剪器
 */
void elog_prune_init(elog_prune_t* p, uint32_t threshold_pct);

/**
 * 从字符串解析裁剪规则 (内部接口)
 * 格式: "~tag1 ~tag2 noisy verbose"
 * ~ 前缀 = 保护, 无前缀 = 低优先级
 * @return ELOG_OK 或错误码
 */
int elog_prune_load_rules(elog_prune_t* p, const char* rules);

/**
 * 将当前规则序列化为字符串 (内部接口)
 * @return 写入的字符数
 */
int elog_prune_serialize_rules(const elog_prune_t* p, char* buf, size_t len);

/**
 * 判断 tag 是否受保护
 */
bool elog_prune_is_protected(const elog_prune_t* p, const char* tag);

/**
 * 判断 tag 是否标记为低优先级
 */
bool elog_prune_is_low_priority(const elog_prune_t* p, const char* tag);

/**
 * 判断当前使用率是否需要触发裁剪
 */
bool elog_prune_should_prune(const elog_prune_t* p, uint32_t buffer_size,
                              uint32_t buffer_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_PRUNE_H */

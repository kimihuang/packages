/**
 * @file elog_prune.c
 * @brief 优先级裁剪实现
 *
 * 解析规则语法:
 *   规则字符串: "~elogd ~crash noisy verbose"
 *   "~" 前缀 → 保护规则 (high_rules)
 *   无前缀   → 低优先级规则 (low_rules)
 *   规则间以空格/逗号/分号分隔
 */

#include "elog_prune.h"
#include "elog_debug.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

void elog_prune_init(elog_prune_t* p, uint32_t threshold_pct) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->threshold_pct = threshold_pct > 0 ? threshold_pct : ELOG_PRUNE_THRESHOLD_PCT;
}

static int parse_single_rule(const char* start, size_t len, elog_prune_rule_t* rule) {
    if (!start || len == 0 || len >= ELOG_MAX_TAG_LEN) return ELOG_ERR_PARAM;

    memset(rule->tag, 0, ELOG_MAX_TAG_LEN);

    size_t i = 0;
    const char* p = start;

    /* 跳过空白 */
    while (i < len && isspace((unsigned char)p[i])) i++;

    /* 检查 ~ 前缀 */
    rule->negated = false;
    if (i < len && p[i] == '~') {
        rule->negated = true;
        i++;
    }

    /* 读取 tag */
    size_t tag_start = i;
    while (i < len && !isspace((unsigned char)p[i]) && p[i] != ',' && p[i] != ';') {
        i++;
    }
    size_t tag_len = i - tag_start;
    if (tag_len == 0 || tag_len >= ELOG_MAX_TAG_LEN) return ELOG_ERR_PARAM;

    memcpy(rule->tag, p + tag_start, tag_len);
    rule->tag[tag_len] = '\0';
    return ELOG_OK;
}

int elog_prune_load_rules(elog_prune_t* p, const char* rules) {
    if (!p || !rules) return ELOG_ERR_PARAM;

    /* 清空旧规则 */
    p->high_count = 0;
    p->low_count = 0;

    const char* start = rules;

    while (*start) {
        /* 跳过分隔符 */
        while (*start && (isspace((unsigned char)*start) || *start == ',' || *start == ';')) {
            start++;
        }
        if (!*start) break;

        /* 找到规则结束 */
        const char* end = start;
        while (*end && !isspace((unsigned char)*end) && *end != ',' && *end != ';') {
            end++;
        }

        elog_prune_rule_t rule;
        int ret = parse_single_rule(start, (size_t)(end - start), &rule);
        if (ret != ELOG_OK) {
            start = end;
            continue;
        }

        if (rule.negated) {
            if (p->high_count < ELOG_MAX_PRUNE_RULES) {
                p->high_rules[p->high_count++] = rule;
            }
        } else {
            if (p->low_count < ELOG_MAX_PRUNE_RULES) {
                p->low_rules[p->low_count++] = rule;
            }
        }

        start = end;
    }

    return ELOG_OK;
}

int elog_prune_serialize_rules(const elog_prune_t* p, char* buf, size_t len) {
    if (!p || !buf || len == 0) return 0;

    int n = 0;
    /* 先输出保护规则 */
    for (uint8_t i = 0; i < p->high_count && (size_t)n < len - 1; i++) {
        n += snprintf(buf + n, len - n, "%s~%s",
                      (n > 0) ? " " : "", p->high_rules[i].tag);
    }
    /* 再输出低优先级规则 */
    for (uint8_t i = 0; i < p->low_count && (size_t)n < len - 1; i++) {
        n += snprintf(buf + n, len - n, " %s", p->low_rules[i].tag);
    }
    return n;
}

bool elog_prune_is_protected(const elog_prune_t* p, const char* tag) {
    if (!p || !tag) return false;
    for (uint8_t i = 0; i < p->high_count; i++) {
        if (strcmp(p->high_rules[i].tag, tag) == 0) {
            return true;
        }
    }
    return false;
}

bool elog_prune_is_low_priority(const elog_prune_t* p, const char* tag) {
    if (!p || !tag) return false;
    for (uint8_t i = 0; i < p->low_count; i++) {
        if (strcmp(p->low_rules[i].tag, tag) == 0) {
            ELOG_DBG_PRUNE("low_priority: tag=\"%s\"", tag);
            return true;
        }
    }
    return false;
}

bool elog_prune_should_prune(const elog_prune_t* p, uint32_t buffer_size,
                              uint32_t buffer_capacity) {
    if (!p || buffer_capacity == 0) return false;
    uint32_t usage_pct = (uint32_t)((uint64_t)buffer_size * 100 / buffer_capacity);
    ELOG_DBG_PRUNE("should_prune: usage=%u%% threshold=%u%%",
                usage_pct, p->threshold_pct);
    return usage_pct >= p->threshold_pct;
}

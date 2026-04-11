/**
 * @file elog_filter.c
 * @brief 日志过滤器实现
 */

#include "elog_filter.h"
#include <string.h>

void elog_filter_init(elog_filter_t* f) {
    if (!f) return;
    f->global_level = ELOG_LEVEL_DEFAULT;
    f->tag_count = 0;
    f->color_enabled = ELOG_COLOR_ENABLE ? true : false;
    f->timestamp_enabled = ELOG_TIMESTAMP_ENABLE ? true : false;
    f->source_location = ELOG_SOURCE_LOCATION ? true : false;
    memset(f->tag_levels, 0, sizeof(f->tag_levels));
}

bool elog_filter_check(const elog_filter_t* f, uint8_t level, const char* tag) {
    if (!f) return false;

    /* 全局级别过滤 */
    if (level < f->global_level) return false;

    /* 标签级别过滤 (更严格) */
    if (tag && f->tag_count > 0) {
        uint8_t tag_level = elog_filter_get_tag_level(f, tag);
        if (level < tag_level) return false;
    }

    return true;
}

void elog_filter_set_global_level(elog_filter_t* f, uint8_t level) {
    if (!f) return;
    if (level > ELOG_LEVEL_NONE) level = ELOG_LEVEL_NONE;
    f->global_level = level;
}

uint8_t elog_filter_get_global_level(const elog_filter_t* f) {
    return f ? f->global_level : ELOG_LEVEL_NONE;
}

int elog_filter_set_tag_level(elog_filter_t* f, const char* tag, uint8_t level) {
    if (!f || !tag) return ELOG_ERR_PARAM;
    if (f->tag_count >= ELOG_MAX_TAGS) return ELOG_ERR_FULL;

    /* 检查是否已存在，存在则更新 */
    for (uint8_t i = 0; i < f->tag_count; i++) {
        if (strcmp(f->tag_levels[i].tag, tag) == 0) {
            f->tag_levels[i].level = level;
            return ELOG_OK;
        }
    }

    /* 新增 */
    f->tag_levels[f->tag_count].tag = tag;
    f->tag_levels[f->tag_count].level = level;
    f->tag_count++;
    return ELOG_OK;
}

int elog_filter_reset_tag_level(elog_filter_t* f, const char* tag) {
    if (!f || !tag) return ELOG_ERR_PARAM;

    for (uint8_t i = 0; i < f->tag_count; i++) {
        if (strcmp(f->tag_levels[i].tag, tag) == 0) {
            /* 移动后续元素 */
            for (uint8_t j = i; j < f->tag_count - 1; j++) {
                f->tag_levels[j] = f->tag_levels[j + 1];
            }
            f->tag_count--;
            return ELOG_OK;
        }
    }
    return ELOG_ERR_PARAM;  /* tag 未找到 */
}

uint8_t elog_filter_get_tag_level(const elog_filter_t* f, const char* tag) {
    if (!f || !tag) return ELOG_LEVEL_NONE;

    for (uint8_t i = 0; i < f->tag_count; i++) {
        if (strcmp(f->tag_levels[i].tag, tag) == 0) {
            return f->tag_levels[i].level;
        }
    }
    return f->global_level;
}

void elog_filter_reset_all(elog_filter_t* f) {
    if (!f) return;
    f->tag_count = 0;
    memset(f->tag_levels, 0, sizeof(f->tag_levels));
}

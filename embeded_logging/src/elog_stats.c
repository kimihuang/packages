/**
 * @file elog_stats.c
 * @brief 日志统计实现
 */

#include "elog_stats.h"
#include <string.h>
#include <stdio.h>

void elog_stats_init(elog_stats_t* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

void elog_stats_on_log(elog_stats_t* s, uint8_t log_id, uint8_t level) {
    if (!s) return;
    if (log_id < ELOG_ID_MAX) {
        s->total_count[log_id]++;
    }
    if (level < 6) {
        s->level_count[level]++;
    }
}

void elog_stats_on_drop(elog_stats_t* s, uint8_t log_id) {
    if (!s) return;
    if (log_id < ELOG_ID_MAX) {
        s->dropped_count[log_id]++;
    }
}

void elog_stats_update_usage(elog_stats_t* s, uint32_t usage) {
    if (!s) return;
    s->buffer_usage = usage;
    if (usage > s->buffer_peak) {
        s->buffer_peak = usage;
    }
}

int elog_stats_get(const elog_stats_t* s, char* buf, size_t len) {
    if (!s || !buf || len == 0) return 0;

    static const char* id_names[] = {
        "main", "radio", "events", "system", "crash", "kernel"
    };

    int n = 0;
    n += snprintf(buf + n, len - n, "elog stats:\n");

    for (int i = 0; i < ELOG_ID_MAX && (size_t)n < len - 1; i++) {
        n += snprintf(buf + n, len - n, "  %-8s: total=%u dropped=%u\n",
                      id_names[i], s->total_count[i], s->dropped_count[i]);
    }

    static const char* lv_names[] = {
        "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    n += snprintf(buf + n, len - n, "  levels: ");
    for (int i = 0; i < 6 && (size_t)n < len - 1; i++) {
        n += snprintf(buf + n, len - n, "%s=%u ", lv_names[i], s->level_count[i]);
    }
    n += snprintf(buf + n, len - n, "\n  buffer: usage=%u peak=%u\n",
                  s->buffer_usage, s->buffer_peak);

    return n;
}

void elog_stats_reset(elog_stats_t* s) {
    if (!s) return;
    uint32_t peak = s->buffer_peak;
    memset(s, 0, sizeof(*s));
    s->buffer_peak = peak;  /* 保留峰值 */
}

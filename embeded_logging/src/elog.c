/**
 * @file elog.c
 * @brief elog 核心实现 — 初始化、日志写入、分发
 *
 * 写入流程:
 *   elog_write() → elog_vwrite() → format → filter → buf.log → transport.dispatch
 */

#include "elog.h"
#include "elog_filter.h"
#include "elog_format.h"
#include "elog_stats.h"
#include "elog_buf.h"
#include "elog_transport.h"
#if ELOG_PRUNE_ENABLE
#include "elog_prune.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

/* ===== 全局状态 ===== */

static struct {
    bool initialized;

    /* 核心 */
    elog_filter_t filter;
    elog_stats_t  stats;
#if ELOG_PRUNE_ENABLE
    elog_prune_t  prune;
#endif

    /* Buffer */
    elog_ring_buf_t ring_buf;
    bool            ring_buf_dynamic;   /* true: destroy 时 free buffer */

    /* Transport */
    elog_transport_registry_t registry;
    elog_stdout_transport_t   stdout_transport;

    /* 后端替换 */
    elog_logger_func_t logger_func;

    /* ISR count */
    _Atomic uint32_t isr_pending_count;
} g_elog;

/* ===== 内部辅助函数 ===== */

static void elog_default_logger(const elog_msg_header_t* hdr,
                                 const char* tag, const char* msg) {
    if (!g_elog.initialized) return;

    /* 写入 RingBuffer */
    g_elog.ring_buf.base.log(&g_elog.ring_buf.base,
                              (elog_id_t)hdr->log_id,
                              (elog_level_t)hdr->level,
                              hdr->pid, hdr->tid, hdr->line,
                              tag, msg);

    /* 更新统计 */
#if ELOG_STATS_ENABLE
    elog_stats_on_log(&g_elog.stats, hdr->log_id, hdr->level);
    size_t buf_size = g_elog.ring_buf.base.size(&g_elog.ring_buf.base);
    elog_stats_update_usage(&g_elog.stats, (uint32_t)buf_size);
#endif
}

/* 将格式化后的文本分发到所有 transport */
static void dispatch_to_transports(const char* text, size_t len) {
    if (!g_elog.initialized) return;
    elog_transport_dispatch(&g_elog.registry,
                            (const uint8_t*)text, len);
}

/* 核心写入路径 */
static void elog_write_internal(elog_level_t level, const char* tag,
                                 const char* file, int line,
                                 const char* fmt, va_list ap) {
    if (!g_elog.initialized) return;

    /* 1. 格式化消息 */
    char msg_buf[ELOG_MAX_MSG_LEN];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);

    /* 2. 过滤检查 */
    if (!elog_filter_check(&g_elog.filter, (uint8_t)level, tag)) {
        return;
    }

    /* 3. 构造 header */
    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = (uint8_t)ELOG_ID_MAIN;
    hdr.level = (uint8_t)level;
    hdr.line = (uint16_t)(line > 0 ? line : 0);
    hdr.pid = (uint16_t)(g_elog.ring_buf.base.capacity ?
                          0 : 0); /* pid/tid 由 ring_buf 内部填充 */
    hdr.tag_len = tag ? (uint16_t)ELOG_MIN(strlen(tag), ELOG_MAX_TAG_LEN - 1) : 0;
    hdr.msg_len = (uint16_t)ELOG_MIN(strlen(msg_buf), ELOG_MAX_MSG_LEN - 1);

    /* 4. 调用后端 (默认: 写入 buffer) */
    if (g_elog.logger_func) {
        g_elog.logger_func(&hdr, tag, msg_buf);
    }

    /* 5. 格式化文本并分发到 transport */
    elog_format_ctx_t fmt_ctx;
    int fmt_len = elog_format_text(&fmt_ctx, &hdr, tag, msg_buf);
    if (fmt_len > 0) {
        dispatch_to_transports(fmt_ctx.buf, (size_t)fmt_len);
    }
}

/* ===== 公共 API ===== */

void (*elog_assert_hook_func)(void) = NULL;

void elog_default_assert_hook(void) {
    abort();
}

int elog_init(void) {
    if (g_elog.initialized) return ELOG_OK;

    memset(&g_elog, 0, sizeof(g_elog));

    /* 初始化 Filter */
    elog_filter_init(&g_elog.filter);

    /* 初始化 Stats */
#if ELOG_STATS_ENABLE
    elog_stats_init(&g_elog.stats);
#endif

    /* 初始化 Prune */
#if ELOG_PRUNE_ENABLE
    elog_prune_init(&g_elog.prune, ELOG_PRUNE_THRESHOLD_PCT);
    elog_prune_load_rules(&g_elog.prune, "~elogd ~crash");
#endif

    /* 初始化 RingBuffer */
    int ret = elog_ring_buf_init(&g_elog.ring_buf, ELOG_BUFFER_SIZE);
    if (ret != ELOG_OK) return ret;
    g_elog.ring_buf_dynamic = true;

    /* 初始化 Transport Registry */
    elog_transport_registry_init(&g_elog.registry);

    /* 注册默认 StdoutTransport */
    elog_stdout_transport_init(&g_elog.stdout_transport);
    elog_stdout_transport_init_fd(&g_elog.stdout_transport, STDOUT_FILENO);
    g_elog.stdout_transport.base.open(&g_elog.stdout_transport.base);
    elog_transport_register(&g_elog.registry, &g_elog.stdout_transport.base);

    /* 设置默认后端 */
    g_elog.logger_func = elog_default_logger;

    /* 设置默认 assert 钩子 */
    elog_assert_hook_func = elog_default_assert_hook;

    g_elog.initialized = true;
    return ELOG_OK;
}

void elog_deinit(void) {
    if (!g_elog.initialized) return;

    /* 注销所有 transport */
    elog_transport_registry_destroy(&g_elog.registry);

    /* 关闭 stdout transport */
    g_elog.stdout_transport.base.close(&g_elog.stdout_transport.base);

    /* 销毁 RingBuffer */
    if (g_elog.ring_buf_dynamic) {
        elog_ring_buf_destroy(&g_elog.ring_buf);
    }

    g_elog.initialized = false;
    g_elog.logger_func = NULL;
}

bool elog_is_initialized(void) {
    return g_elog.initialized;
}

/* ===== 级别管理 ===== */

void elog_set_level(elog_level_t level) {
    if (!g_elog.initialized) return;
    elog_filter_set_global_level(&g_elog.filter, (uint8_t)level);
}

elog_level_t elog_get_level(void) {
    if (!g_elog.initialized) return ELOG_LEVEL_NONE;
    return (elog_level_t)elog_filter_get_global_level(&g_elog.filter);
}

int elog_set_tag_level(const char* tag, elog_level_t level) {
    if (!g_elog.initialized) return ELOG_ERR_NOT_INIT;
    return elog_filter_set_tag_level(&g_elog.filter, tag, (uint8_t)level);
}

int elog_reset_tag_level(const char* tag) {
    if (!g_elog.initialized) return ELOG_ERR_NOT_INIT;
    return elog_filter_reset_tag_level(&g_elog.filter, tag);
}

/* ===== 日志输出 ===== */

void elog_vwrite(elog_level_t level, const char* tag, const char* fmt, va_list ap) {
    elog_write_internal(level, tag, NULL, 0, fmt, ap);
}

void elog_write(elog_level_t level, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_vwrite(level, tag, fmt, ap);
    va_end(ap);
}

void elog_verbose(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_write_internal(ELOG_LEVEL_VERBOSE, tag, NULL, 0, fmt, ap);
    va_end(ap);
}

void elog_debug(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_write_internal(ELOG_LEVEL_DEBUG, tag, NULL, 0, fmt, ap);
    va_end(ap);
}

void elog_info(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_write_internal(ELOG_LEVEL_INFO, tag, NULL, 0, fmt, ap);
    va_end(ap);
}

void elog_warn(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_write_internal(ELOG_LEVEL_WARN, tag, NULL, 0, fmt, ap);
    va_end(ap);
}

void elog_error(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_write_internal(ELOG_LEVEL_ERROR, tag, NULL, 0, fmt, ap);
    va_end(ap);
}

void elog_fatal(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    elog_write_internal(ELOG_LEVEL_FATAL, tag, NULL, 0, fmt, ap);
    va_end(ap);
}

/* ===== ISR 安全日志 ===== */

int elog_write_isr(elog_level_t level, const char* tag,
                   const char* msg, uint16_t msg_len) {
    if (!g_elog.initialized) return ELOG_ERR_NOT_INIT;
    if (!tag || !msg || msg_len == 0) return ELOG_ERR_PARAM;

    int ret = elog_ring_buf_log_isr(&g_elog.ring_buf,
                                     ELOG_ID_MAIN, level,
                                     0, 0, 0, tag, msg);
    if (ret == ELOG_OK) {
        atomic_fetch_add(&g_elog.isr_pending_count, 1);
    }
    return ret;
}

/* ===== 后端替换 ===== */

void elog_set_logger(elog_logger_func_t func) {
    g_elog.logger_func = func;
}

/* ===== Transport 注册 ===== */

int elog_add_transport(void* transport) {
    if (!g_elog.initialized) return ELOG_ERR_NOT_INIT;
    return elog_transport_register(&g_elog.registry,
                                    (elog_transport_t*)transport);
}

int elog_remove_transport(void* transport) {
    if (!g_elog.initialized) return ELOG_ERR_NOT_INIT;
    return elog_transport_unregister(&g_elog.registry,
                                      (elog_transport_t*)transport);
}

/* ===== 统计 ===== */

int elog_get_stats(char* buf, size_t len) {
#if ELOG_STATS_ENABLE
    return elog_stats_get(&g_elog.stats, buf, len);
#else
    (void)buf; (void)len;
    return 0;
#endif
}

void elog_reset_stats(void) {
#if ELOG_STATS_ENABLE
    elog_stats_reset(&g_elog.stats);
#endif
}

/* ===== 裁剪 ===== */

int elog_prune_set_rules(const char* rules) {
#if ELOG_PRUNE_ENABLE
    if (!g_elog.initialized) return ELOG_ERR_NOT_INIT;
    return elog_prune_load_rules(&g_elog.prune, rules);
#else
    (void)rules;
    return ELOG_ERR_PARAM;
#endif
}

int elog_prune_get_rules(char* buf, size_t len) {
#if ELOG_PRUNE_ENABLE
    return elog_prune_serialize_rules(&g_elog.prune, buf, len);
#else
    (void)buf; (void)len;
    return 0;
#endif
}

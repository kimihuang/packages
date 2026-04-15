/**
 * @file elog.h
 * @brief elog 公共 API — 嵌入式日志框架入口
 *
 * 使用示例:
 *   elog_init();
 *   elog_set_level(ELOG_LEVEL_INFO);
 *   elog_info("sensor", "temperature=%d", 25);
 *   ELOG_I("sensor", "status=OK");   // 编译期过滤版本
 *   elog_deinit();
 */

#ifndef ELOG_H
#define ELOG_H

#include "elog_def.h"
#include "elog_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 生命周期 ===== */

/**
 * 初始化日志系统
 * 创建默认的 RingBuffer、Filter、Stats、Registry，
 * 注册默认 StdoutTransport。
 */
int elog_init(void);

/**
 * 反初始化日志系统
 * 销毁所有资源。
 */
void elog_deinit(void);

/**
 * 检查是否已初始化
 */
bool elog_is_initialized(void);

/* ===== 日志级别 ===== */

/**
 * 设置全局最低级别
 */
void elog_set_level(elog_level_t level);

/**
 * 获取全局最低级别
 */
elog_level_t elog_get_level(void);

/**
 * 设置标签级别
 */
int elog_set_tag_level(const char* tag, elog_level_t level);

/**
 * 重置标签级别
 */
int elog_reset_tag_level(const char* tag);

/* ===== 日志输出 ===== */

/**
 * 写入日志
 */
void elog_write(elog_level_t level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * 写入日志到指定 buffer
 * @param log_id  目标 buffer ID (ELOG_ID_MAIN, ELOG_ID_RADIO, ...)
 */
void elog_write_ex(elog_id_t log_id, elog_level_t level,
                   const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * 写入日志 (va_list 版本)
 */
void elog_vwrite(elog_level_t level, const char* tag, const char* fmt, va_list ap);

/* ===== 级别快捷方式 (函数版本) ===== */

void elog_verbose(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void elog_debug(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void elog_info(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void elog_warn(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void elog_error(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void elog_fatal(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* ===== 宏版本 (编译期级别过滤, 零开销) ===== */

#define ELOG_V(tag, fmt, ...) \
    do { if (ELOG_LEVEL_VERBOSE >= ELOG_LEVEL_DEFAULT) \
        elog_verbose(tag, fmt, ##__VA_ARGS__); } while(0)

#define ELOG_D(tag, fmt, ...) \
    do { if (ELOG_LEVEL_DEBUG >= ELOG_LEVEL_DEFAULT) \
        elog_debug(tag, fmt, ##__VA_ARGS__); } while(0)

#define ELOG_I(tag, fmt, ...) \
    do { if (ELOG_LEVEL_INFO >= ELOG_LEVEL_DEFAULT) \
        elog_info(tag, fmt, ##__VA_ARGS__); } while(0)

#define ELOG_W(tag, fmt, ...) \
    do { if (ELOG_LEVEL_WARN >= ELOG_LEVEL_DEFAULT) \
        elog_warn(tag, fmt, ##__VA_ARGS__); } while(0)

#define ELOG_E(tag, fmt, ...) \
    do { if (ELOG_LEVEL_ERROR >= ELOG_LEVEL_DEFAULT) \
        elog_error(tag, fmt, ##__VA_ARGS__); } while(0)

#define ELOG_F(tag, fmt, ...) \
    do { if (ELOG_LEVEL_FATAL >= ELOG_LEVEL_DEFAULT) \
        elog_fatal(tag, fmt, ##__VA_ARGS__); } while(0)

/* ===== ISR 安全日志 ===== */

/**
 * ISR 安全写入 (消息需预格式化, 不使用 printf)
 */
int elog_write_isr(elog_level_t level, const char* tag,
                   const char* msg, uint16_t msg_len);

/**
 * ISR 安全写入便捷宏 (消息为字符串字面量, 不使用 printf)
 */
#define elog_isr(level, tag, msg) \
    elog_write_isr(level, tag, msg, (uint16_t)strlen(msg))

#define ELOG_ISR_V(tag, msg) \
    do { if (ELOG_LEVEL_VERBOSE >= ELOG_LEVEL_DEFAULT) \
        elog_isr(ELOG_LEVEL_VERBOSE, tag, msg); } while(0)

#define ELOG_ISR_D(tag, msg) \
    do { if (ELOG_LEVEL_DEBUG >= ELOG_LEVEL_DEFAULT) \
        elog_isr(ELOG_LEVEL_DEBUG, tag, msg); } while(0)

#define ELOG_ISR_I(tag, msg) \
    do { if (ELOG_LEVEL_INFO >= ELOG_LEVEL_DEFAULT) \
        elog_isr(ELOG_LEVEL_INFO, tag, msg); } while(0)

#define ELOG_ISR_W(tag, msg) \
    do { if (ELOG_LEVEL_WARN >= ELOG_LEVEL_DEFAULT) \
        elog_isr(ELOG_LEVEL_WARN, tag, msg); } while(0)

#define ELOG_ISR_E(tag, msg) \
    do { if (ELOG_LEVEL_ERROR >= ELOG_LEVEL_DEFAULT) \
        elog_isr(ELOG_LEVEL_ERROR, tag, msg); } while(0)

#define ELOG_ISR_F(tag, msg) \
    do { if (ELOG_LEVEL_FATAL >= ELOG_LEVEL_DEFAULT) \
        elog_isr(ELOG_LEVEL_FATAL, tag, msg); } while(0)

/* ===== 后端替换 ===== */

/**
 * 日志后端函数类型
 */
typedef void (*elog_logger_func_t)(const elog_msg_header_t* hdr,
                                    const char* tag,
                                    const char* msg);

/**
 * 替换日志后端 (策略模式)
 */
void elog_set_logger(elog_logger_func_t func);

/* ===== Transport 注册 ===== */

/**
 * 注册传输目标
 */
int elog_add_transport(void* transport);

/**
 * 注销传输目标
 */
int elog_remove_transport(void* transport);

/* ===== 统计 ===== */

/**
 * 获取统计摘要
 */
int elog_get_stats(char* buf, size_t len);

/**
 * 重置统计
 */
void elog_reset_stats(void);

/* ===== 裁剪 ===== */

/**
 * 设置裁剪规则
 */
int elog_prune_set_rules(const char* rules);

/**
 * 获取裁剪规则
 */
int elog_prune_get_rules(char* buf, size_t len);

/* ===== 断言 ===== */

/**
 * 断言失败钩子 (用户可覆盖)
 */
extern void (*elog_assert_hook_func)(void);

/**
 * 默认断言钩子: abort()
 */
void elog_default_assert_hook(void);

#define elog_assert(cond) \
    do { \
        if (!(cond)) { \
            elog_fatal("assert", "%s failed at %s:%d", \
                       #cond, __FILE__, __LINE__); \
            if (elog_assert_hook_func) elog_assert_hook_func(); \
        } \
    } while(0)

/* ===== Reader ===== */

/**
 * 获取全局 ReaderList (elog_init 后可用)
 */
elog_reader_list_t* elog_get_reader_list(void);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_H */

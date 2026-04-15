/**
 * @file elog_config.h
 * @brief elog 编译期配置（用户可修改）
 *
 * 所有可通过编译期裁剪的功能开关和尺寸参数都在此定义。
 */

#ifndef ELOG_CONFIG_H
#define ELOG_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 缓冲区配置 ===== */
#ifndef ELOG_BUFFER_SIZE
#define ELOG_BUFFER_SIZE          65536    /* Ring buffer 大小 (bytes) */
#endif

#ifndef ELOG_MAX_MSG_LEN
#define ELOG_MAX_MSG_LEN          1024     /* 单条日志最大长度 (不含 header) */
#endif

#ifndef ELOG_MAX_TAG_LEN
#define ELOG_MAX_TAG_LEN          32       /* 标签最大长度 */
#endif

#ifndef ELOG_MAX_FORMAT_LEN
#define ELOG_MAX_FORMAT_LEN       (ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN + 128)
#endif

/* ===== 功能裁剪 ===== */
#ifndef ELOG_COLOR_ENABLE
#define ELOG_COLOR_ENABLE         1        /* 彩色输出 (ANSI escape) */
#endif

#ifndef ELOG_TIMESTAMP_ENABLE
#define ELOG_TIMESTAMP_ENABLE     1        /* 时间戳 */
#endif

#ifndef ELOG_SOURCE_LOCATION
#define ELOG_SOURCE_LOCATION      0        /* 源码位置 (file:line) */
#endif

#ifndef ELOG_EVENT_ENABLE
#define ELOG_EVENT_ENABLE         1        /* Event 日志 (TLV 二进制) */
#endif

#ifndef ELOG_STATS_ENABLE
#define ELOG_STATS_ENABLE         1        /* 统计功能 */
#endif

#ifndef ELOG_PRUNE_ENABLE
#define ELOG_PRUNE_ENABLE         1        /* 优先级裁剪 */
#endif

/* ===== 过滤配置 ===== */
#ifndef ELOG_MAX_TAGS
#define ELOG_MAX_TAGS             16       /* 标签级别映射最大数量 */
#endif

/* ===== 传输层 ===== */
#ifndef ELOG_MAX_TRANSPORTS
#define ELOG_MAX_TRANSPORTS       4        /* 最大同时注册的传输目标 */
#endif

/* ===== 裁剪配置 ===== */
#ifndef ELOG_MAX_PRUNE_RULES
#define ELOG_MAX_PRUNE_RULES      16       /* 最大裁剪规则数 */
#endif

#ifndef ELOG_PRUNE_THRESHOLD_PCT
#define ELOG_PRUNE_THRESHOLD_PCT  90       /* 触发裁剪的缓冲区使用率 (%) */
#endif

/* ===== 读者配置 ===== */
#ifndef ELOG_MAX_READERS
#define ELOG_MAX_READERS          4        /* 最大同时读者数 */
#endif

/* ===== 默认日志级别 ===== */
#ifndef ELOG_LEVEL_DEFAULT
#define ELOG_LEVEL_DEFAULT        ELOG_LEVEL_DEBUG
#endif

/* ===== Event 日志配置 ===== */
#ifndef ELOG_EVENT_MAX_DEPTH
#define ELOG_EVENT_MAX_DEPTH      8        /* Event LIST 最大嵌套深度 */
#endif

#ifndef ELOG_EVENT_STORAGE_SIZE
#define ELOG_EVENT_STORAGE_SIZE   4096     /* Event 编码缓冲区大小 */
#endif

/* ===== FileTransport 配置 ===== */
#ifndef ELOG_FILE_PATH_MAX
#define ELOG_FILE_PATH_MAX        256      /* 日志文件路径最大长度 */
#endif

#ifndef ELOG_FILE_DEFAULT_SIZE
#define ELOG_FILE_DEFAULT_SIZE    (1*1024*1024)  /* 默认单文件最大 1MB */
#endif

#ifndef ELOG_FILE_MAX_FILES
#define ELOG_FILE_MAX_FILES       5        /* 默认保留的最大轮转文件数 */
#endif

#ifndef ELOG_FILE_DEFAULT_PATH
#define ELOG_FILE_DEFAULT_PATH    "elog.log"
#endif

#ifdef __cplusplus
}
#endif

/* ===== 平台选择 ===== */
#ifndef ELOG_PORT_LINUX
#define ELOG_PORT_LINUX            1        /* 1: Linux, 0: Bare-metal */
#endif

/* ===== elogd 守护进程配置 ===== */
#ifndef ELOG_DAEMON_ENABLE
#define ELOG_DAEMON_ENABLE         1        /* 客户端: 通过 socket 发送日志到 elogd */
#endif

#endif /* ELOG_CONFIG_H */

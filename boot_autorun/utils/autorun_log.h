/**
 * @file autorun_log.h
 * @brief 统一日志系统 - 头文件
 *
 * 本文件定义了自动运行框架的统一日志接口，
 * 为所有阶段（BL1、BL31、RT-Thread、U-Boot、Linux）提供一致的日志输出能力。
 *
 * 设计目标:
 * 1. **统一接口**: 所有阶段使用相同的 API，降低学习成本
 * 2. **平台适配**: 自动适配各平台的底层输出机制
 * 3. **级别控制**: 支持多级日志过滤（OFF/ERROR/WARN/INFO/DEBUG/VERBOSE）
 * 4. **格式统一**: 自动添加时间戳、模块名、级别前缀等
 * 5. **零开销**: 编译期优化，关闭日志时不产生任何运行时开销
 * 6. **线程安全**: 在多线程环境下安全使用（如 RT-Thread、Linux）
 *
 * 日志级别层次:
 * ┌─────────────────────────────────────────────┐
 * │  LOG_VERBOSE (5) - 极其详细的跟踪信息       │
 * ├─────────────────────────────────────────────┤
 * │  LOG_DEBUG   (4) - 调试信息                 │
 * ├─────────────────────────────────────────────┤
 * │  LOG_INFO    (3) - 一般信息（推荐生产环境） │
 * ├─────────────────────────────────────────────┤
 * │  LOG_WARN    (2) - 警告信息                 │
 * ├─────────────────────────────────────────────┤
 * │  LOG_ERROR   (1) - 错误信息                 │
 * ├─────────────────────────────────────────────┤
 * │  LOG_OFF     (0) - 关闭所有日志             │
 * └─────────────────────────────────────────────┘
 *
 * 使用方式:
 * @code
 *   #include "utils/autorun_log.h"
 *
 *   void my_function(void) {
 *       // 基础用法：自动添加换行符
 *       AUTORUN_LOG_INFO("Initializing module...");
 *       AUTORUN_LOG_ERROR("Failed to open file: %s", filename);
 *
 *       // 条件日志（仅在特定级别启用时编译）
 *       #if AUTORUN_LOG_LEVEL >= AUTORUN_LOG_DEBUG
 *       AUTORUN_LOG_DEBUG("Debug value: %d", value);
 *       #endif
 *   }
 * @endcode
 *
 * 平台适配说明:
 * - BL1/BL31:   通过 ATF 的 tf_log() / INFO()/ERROR() 输出
 * - RT-Thread:  通过 rt_kprintf() / LOG_x() 输出
 * - U-Boot:     通过 printf() / debug() 输出
 * - Linux:      通过 printf() / syslog() 输出
 *
 * 集成方式:
 * 1. 在 adapter 的 *_adapter.c 中调用 autorun_log_init()
 * 2. 设置当前阶段标识 (AUTORUN_STAGE_BL1 等)
 * 3. 可选：配置自定义日志回调函数
 * 4. Core 层直接使用 AUTORUN_LOG_xxx 宏
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef AUTORUN_LOG_H
#define AUTORUN_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*                    编译期配置                                      */
/* ================================================================== */

/**
 * @brief 全局日志级别（编译时常量）
 *
 * 设置此宏可以完全禁用低级别的日志代码，实现零开销。
 * 只有 <= 此级别的日志语句才会被编译进二进制文件。
 *
 * 推荐设置:
 * - 生产环境: 3 (LOG_INFO)
 * - 调试环境: 4 (LOG_DEBUG)
 * - 性能分析: 5 (LOG_VERBOSE)
 * - 关闭日志: 0 (LOG_OFF)
 */
#ifndef AUTORUN_LOG_LEVEL
#define AUTORUN_LOG_LEVEL           3   /* 默认 INFO 级别 */
#endif

/**
 * @brief 启用带颜色的日志输出
 *
 * 定义后会为不同级别添加 ANSI 颜色码。
 * 仅在支持 ANSI 转义码的终端上有效。
 */
// #define AUTORUN_LOG_COLOR

/**
 * @brief 启用函数名和行号输出
 *
 * 定义后每条日志会包含源文件位置信息。
 * 会增加二进制大小，建议仅调试时启用。
 */
// #define AUTORUN_LOG_LOCATION

/**
 * @brief 启用时间戳输出
 *
 * 定义后每条日志会包含高精度时间戳。
 * 注意：在某些嵌入式环境中获取时间戳可能有性能开销。
 */
#ifndef AUTORUN_LOG_TIMESTAMP
#define AUTORUN_LOG_TIMESTAMP        1   /* 默认启用 */
#endif

/**
 * @brief 日志消息最大长度（含终止符）
 *
 * 单条日志消息的最大缓冲区大小。
 * 超过此长度的消息会被截断。
 */
#ifndef AUTORUN_LOG_MAX_MSG_LEN
#define AUTORUN_LOG_MAX_MSG_LEN      512
#endif

/**
 * @brief 日志前缀最大长度
 *
 * 用于存储 [时间] [级别] [模块] 等前缀信息。
 */
#ifndef AUTORUN_LOG_PREFIX_LEN
#define AUTORUN_LOG_PREFIX_LEN       128
#endif

/* ================================================================== */
/*                    日志级别定义                                    */
/* ================================================================== */

/**
 * @brief 日志级别枚举
 *
 * 与 autorun_config.h 中的 log_level_t 保持一致。
 */
typedef enum {
    /** 关闭所有日志输出 */
    AUTORUN_LOG_OFF     = 0,
    /** 错误信息（必须关注）*/
    AUTORUN_LOG_ERROR   = 1,
    /** 警告信息（可能有问题）*/
    AUTORUN_LOG_WARN    = 2,
    /** 一般信息（正常运行状态）*/
    AUTORUN_LOG_INFO    = 3,
    /** 调试信息（开发调试用）*/
    AUTORUN_LOG_DEBUG   = 4,
    /** 极其详细的信息（性能分析用）*/
    AUTORUN_LOG_VERBOSE = 5
} autorun_log_level_t;

/* ================================================================== */
/*                    运行阶段标识                                    */
/* ================================================================== */

/**
 * @brief 当前运行的启动阶段
 *
 * 用于在日志中标识当前执行上下文。
 */
typedef enum {
    /** 未初始化或未知阶段 */
    AUTORUN_STAGE_UNKNOWN  = 0,
    /** ATF BL1 阶段 */
    AUTORUN_STAGE_BL1      = 1,
    /** ATF BL31 阶段 */
    AUTORUN_STAGE_BL31     = 2,
    /** RT-Thread 操作系统阶段 */
    AUTORUN_STAGE_RTTHREAD = 3,
    /** U-Boot 引导加载程序阶段 */
    AUTORUN_STAGE_UBOOT    = 4,
    /** Linux 用户态阶段 */
    AUTORUN_STAGE_LINUX    = 5,
    /** 自定义/扩展阶段 */
    AUTORUN_STAGE_CUSTOM   = 99
} autorun_stage_t;

/* ================================================================== */
/*                    回调函数类型                                    */
/* ================================================================== */

/**
 * @brief 日志输出回调函数类型
 *
 * 允许用户自定义日志的最终输出目标。
 * 例如：写入文件、发送到网络、显示到GUI等。
 *
 * @param level    日志级别
 * @param stage    当前阶段
 * @param formatted_msg 已格式化的完整消息（含前缀）
 * @param user_data 用户自定义数据（注册回调时传入）
 */
typedef void (*autorun_log_output_fn_t)(int level,
                                        autorun_stage_t stage,
                                        const char *formatted_msg,
                                        void *user_data);

/* ================================================================== */
/*                    核心API函数声明                                 */
/* ================================================================== */

/**
 * @brief 初始化日志系统
 *
 * 必须在使用任何日志宏之前调用一次。
 * 设置当前运行阶段和可选的自定义配置。
 *
 * @param[in] stage 当前运行阶段 (AUTORUN_STAGE_BL1 等)
 * @return int 0=成功, -1=参数错误
 */
int autorun_log_init(autorun_stage_t stage);

/**
 * @brief 初始化日志系统（带自定义回调）
 *
 * 允许指定自定义的日志输出函数。
 *
 * @param[in] stage      当前运行阶段
 * @param[in] output_fn  自定义输出函数 (NULL 则使用默认输出)
 * @param[in] user_data  传递给 output_fn 的用户数据
 * @return int 0=成功, -1=参数错误
 */
int autorun_log_init_ex(autorun_stage_t stage,
                        autorun_log_output_fn_t output_fn,
                        void *user_data);

/**
 * @brief 关闭日志系统
 *
 * 释放资源，重置内部状态。
 * 通常在程序退出前调用。
 */
void autorun_log_deinit(void);

/**
 * @brief 设置全局日志过滤级别
 *
 * 动态调整日志输出级别。只有 <= 此级别的日志才会被输出。
 *
 * @param[in] level 新的日志级别 (AUTORUN_LOG_OFF ~ AUTORUN_LOG_VERBOSE)
 */
void autorun_log_set_level(int level);

/**
 * @brief 获取当前日志过滤级别
 *
 * @return int 当前日志级别
 */
int autorun_log_get_level(void);

/**
 * @brief 设置当前运行阶段
 *
 * 动态切换运行阶段标识（用于日志前缀）。
 *
 * @param[in] stage 新的阶段标识
 */
void autorun_log_set_stage(autorun_stage_t stage);

/**
 * @brief 获取当前运行阶段
 *
 * @return autorun_stage_t 当前阶段
 */
autorun_stage_t autorun_log_get_stage(void);

/**
 * @brief 核心日志输出函数（供内部和高级用户使用）
 *
 * 格式化并输出一条日志消息。
 * 建议优先使用下方的便捷宏。
 *
 * @param[in] level 日志级别
 * @param[in] fmt   格式字符串 (同 printf)
 * @param[in] ...   可变参数列表
 */
void autorun_log_output(int level, const char *fmt, ...);

/**
 * @brief 核心日志输出函数（va_list 版本）
 *
 * 用于从其他变参函数中调用。
 *
 * @param[in] level 日志级别
 * @param[in] fmt   格式字符串
 * @param[in] args  可变参数列表
 */
void autorun_log_output_v(int level, const char *fmt, va_list args);

/* ================================================================== */
/*                    便捷宏定义                                     */
/* ================================================================== */

/*
 * 以下宏提供简洁的日志记录方式：
 * - 自动添加换行符
 * - 编译期级别检查（低于 AUTORUN_LOG_LEVEL 的宏被定义为空操作）
 * - 支持 printf 风格的格式化参数
 */

#if AUTORUN_LOG_LEVEL >= 5

/**
 * @brief 输出 VERBOSE 级别日志
 *
 * 极其详细的信息，用于性能分析、详细跟踪等场景。
 * 生产环境通常应禁用此级别以减少I/O开销。
 */
#define AUTORUN_LOG_VERBOSE(fmt, ...) \
    autorun_log_output(AUTORUN_LOG_VERBOSE, fmt "\n", ##__VA_ARGS__)

#else

#define AUTORUN_LOG_VERBOSE(fmt, ...) ((void)0)

#endif /* AUTORUN_LOG_LEVEL >= 5 */


#if AUTORUN_LOG_LEVEL >= 4

/**
 * @brief 输出 DEBUG 级别日志
 *
 * 调试信息，用于开发和问题排查。
 * 包含变量值、函数调用顺序、中间结果等详细信息。
 */
#define AUTORUN_LOG_DEBUG(fmt, ...) \
    autorun_log_output(AUTORUN_LOG_DEBUG, fmt "\n", ##__VA_ARGS__)

#else

#define AUTORUN_LOG_DEBUG(fmt, ...) ((void)0)

#endif /* AUTORUN_LOG_LEVEL >= 4 */


#if AUTORUN_LOG_LEVEL >= 3

/**
 * @brief 输出 INFO 级别日志
 *
 * 一般信息，描述程序的正常运行状态。
 * 例如：初始化完成、任务开始/结束、关键节点等。
 * 这是推荐的**默认生产环境日志级别**。
 */
#define AUTORUN_LOG_INFO(fmt, ...) \
    autorun_log_output(AUTORUN_LOG_INFO, fmt "\n", ##__VA_ARGS__)

#else

#define AUTORUN_LOG_INFO(fmt, ...) ((void)0)

#endif /* AUTORUN_LOG_LEVEL >= 3 */


#if AUTORUN_LOG_LEVEL >= 2

/**
 * @brief 输出 WARN 级别日志
 *
 * 警告信息，表示潜在的问题或不理想的情况。
 * 程序仍可继续运行，但可能需要注意。
 * 例如：使用了默认值、资源紧张、非致命错误恢复等。
 */
#define AUTORUN_LOG_WARN(fmt, ...) \
    autorun_log_output(AUTORUN_LOG_WARN, fmt "\n", ##__VA_ARGS__)

#else

#define AUTORUN_LOG_WARN(fmt, ...) ((void)0)

#endif /* AUTORUN_LOG_LEVEL >= 2 */


#if AUTORUN_LOG_LEVEL >= 1

/**
 * @brief 输出 ERROR 级别日志
 *
 * 错误信息，表示发生了严重问题。
 * 可能导致功能失败或异常终止。
 * **必须关注和处理此类日志**。
 */
#define AUTORUN_LOG_ERROR(fmt, ...) \
    autorun_log_output(AUTORUN_LOG_ERROR, fmt "\n", ##__VA_ARGS__)

#else

#define AUTORUN_LOG_ERROR(fmt, ...) ((void)0)

#endif /* AUTORUN_LOG_LEVEL >= 1 */


/* ================================================================== */
/*                    条件日志宏                                     */
/* ================================================================== */

/**
 * @brief 条件日志宏（所有级别通用）
 *
 * 仅当条件表达式为真时才输出日志。
 * 用于减少不必要的日志计算开销。
 *
 * @param condition 条件表达式（必须在编译期确定或简单运行时检查）
 * @param level     日志级别
 * @param fmt       格式字符串
 * @param ...       可变参数
 */
#define AUTORUN_LOG_IF(condition, level, fmt, ...) \
    do { \
        if (condition) { \
            autorun_log_output((level), fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief 一次性日志宏
 *
 * 仅输出一次（使用 static 变量标记）。
 * 适用于循环中的"首次出现"类提示信息。
 *
 * @param level 日志级别
 * @param fmt   格式字符串
 * @param ...   可变参数
 */
#define AUTORUN_LOG_ONCE(level, fmt, ...) \
    do { \
        static bool _log_once_flag_ = false; \
        if (!_log_once_flag_) { \
            _log_once_flag_ = true; \
            autorun_log_output((level), fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

/* ================================================================== */
/*                    平台特定快捷宏                                 */
/* ================================================================== */

/**
 * @brief 使用平台原生格式的日志输出
 *
 * 不添加任何前缀，直接调用平台的 log 函数。
 * 用于需要与平台日志风格保持一致的场景。
 *
 * 注意：这些宏不受 AUTORUN_LOG_LEVEL 控制，
 *       但受平台自身的日志级别控制。
 */

/** BL1/BL31 阶段专用（ATF 格式）*/
#if defined(PLATFORM_BL1) || defined(PLATFORM_BL31)
    #define AUTORUN_NOTICE(fmt, ...) NOTICE(fmt, ##__VA_ARGS__)
    #define AUTORUN_TF_ERROR(fmt, ...) ERROR(fmt, ##__VA_ARGS__)
    #define AUTORUN_TF_WARN(fmt, ...) WARN(fmt, ##__VA_ARGS__)
    #define AUTORUN_TF_INFO(fmt, ...) INFO(fmt, ##__VA_ARGS__)
#endif

/** RT-Thread 阶段专用 */
#if defined(PLATFORM_RTTHREAD)
    #define AUTORUN_RT_LOG_E(fmt, ...) LOG_E(fmt, ##__VA_ARGS__)
    #define AUTORUN_RT_LOG_W(fmt, ...) LOG_W(fmt, ##__VA_ARGS__)
    #define AUTORUN_RT_LOG_I(fmt, ...) LOG_I(fmt, ##__VA_ARGS__)
    #define AUTORUN_RT_LOG_D(fmt, ...) LOG_D(fmt, ##__VA_ARGS__)
#endif

/** U-Boot 阶段专用 */
#if defined(PLATFORM_UBOOT)
    #define AUTORUN_UBOOT_DEBUG(fmt, ...) debug(fmt, ##__VA_ARGS__)
#endif

/** Linux 阶段专用 */
#if defined(PLATFORM_LINUX)
    #ifdef USE_SYSLOG
        #define AUTORUN_SYSLOG_ERR(fmt, ...) syslog(LOG_ERR, fmt, ##__VA_ARGS__)
        #define AUTORUN_SYSLOG_WARN(fmt, ...) syslog(LOG_WARNING, fmt, ##__VA_ARGS__)
        #define AUTORUN_SYSLOG_INFO(fmt, ...) syslog(LOG_INFO, fmt, ##__VA_ARGS__)
        #define AUTORUN_SYSLOG_DEBUG(fmt, ...) syslog(LOG_DEBUG, fmt, ##__VA_ARGS__)
    #endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* AUTORUN_LOG_H */

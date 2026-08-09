/**
 * @file autorun_log.c
 * @brief 统一日志系统 - 实现文件
 *
 * 本文件实现了自动运行框架的统一日志系统，
 * 提供跨平台的日志输出能力，并自动适配各阶段的底层机制。
 *
 * 核心功能:
 * 1. **多级别日志**: OFF/ERROR/WARN/INFO/DEBUG/VERBOSE 五级过滤
 * 2. **平台自适应**: 自动检测并使用当前平台的输出函数
 * 3. **格式化输出**: 自动添加时间戳、阶段名、级别前缀等
 * 4. **回调支持**: 允许自定义日志输出目标（文件、网络等）
 * 5. **编译期优化**: 通过宏控制实现零开销日志
 * 6. **线程安全**: 使用互斥锁保护共享状态（多线程环境）
 *
 * 平台适配策略:
 * ┌─────────────┬──────────────────┬────────────────────────────┐
 * │   阶段      │   检测宏          │   输出函数                  │
 * ├─────────────┼──────────────────┼────────────────────────────┤
 * │ BL1/BL31    │ PLATFORM_BL1     │ tf_log / INFO / ERROR      │
 * │             │ PLATFORM_BL31    │                            │
 * ├─────────────┼──────────────────┼────────────────────────────┤
 * │ RT-Thread   │ PLATFORM_RTTHREAD│ rt_kprintf / LOG_x         │
 * ├─────────────┼──────────────────┼────────────────────────────┤
 * │ U-Boot      │ PLATFORM_UBOOT   │ printf / debug             │
 * ├─────────────┼──────────────────┼────────────────────────────┤
 * │ Linux       │ PLATFORM_LINUX   │ printf / syslog            │
 * ├─────────────┼──────────────────┼────────────────────────────┤
 * │ 默认/未知   │ (无)             │ printf (回退方案)           │
 * └─────────────┴──────────────────┴────────────────────────────┘
 *
 * 使用示例:
 * @code
 *   // 在 adapter 初始化时调用
 *   #include "utils/autorun_log.h"
 *
 *   // BL1 adapter 中:
 *   autorun_log_init(AUTORUN_STAGE_BL1);
 *
 *   // Core 层代码中直接使用宏:
 *   AUTORUN_LOG_INFO("Task started: %s", task_name);
 *   AUTORUN_LOG_ERROR("Failed with code %d", error_code);
 *
 *   // 带条件判断的日志:
 *   if (error_count > 10) {
 *       AUTORUN_LOG_WARN("Too many errors (%d)", error_count);
 *   }
 * @endcode
 *
 * 性能考虑:
 * - 编译期过滤：低于 AUTORUN_LOG_LEVEL 的宏展开为空操作
 * - 运行期过滤：autorun_log_set_level() 动态调整
 * - 缓冲区复用：使用静态缓冲区避免频繁分配
 * - 最小化系统调用：批量构建消息后一次性输出
 *
 * 线程安全:
 * - 在 RT-Thread/Linux 等多线程环境中使用互斥锁保护
 * - 单线程环境（BL1/BL31/U-Boot）中锁操作为空实现
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "autorun_log.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/*                    平台相关头文件                                  */
/* ================================================================== */

/*
 * 条件包含各平台的头文件和API声明
 * 通过预处理器宏检测当前编译环境
 */

#if defined(PLATFORM_BL1) || defined(PLATFORM_BL31)
    /* ATF (ARM Trusted Firmware) 环境 */
    #include <common/debug.h>       /* INFO, WARN, ERROR, NOTICE */
    #include <lib/libc/string.h>    /* snprintf, strlen */
#endif

#if defined(PLATFORM_RTTHREAD)
    /* RT-Thread 操作系统 */
    #include <rtthread.h>           /* rt_kprintf */
    #ifdef RT_USING_ULOG
        #include <ulog.h>           /* ulog 框架 (可选) */
    #endif
#endif

#if defined(PLATFORM_UBOOT)
    /* U-Boot 引导加载程序 */
    #include <common.h>             /* printf, debug */
    #include <linux/string.h>       /* snprintf */
#endif

#if defined(PLATFORM_LINUX) || (!defined(PLATFORM_BL1) && \
    !defined(PLATFORM_BL31) && !defined(PLATFORM_RTTHREAD) && \
    !defined(PLATFORM_UBOOT))
    /* Linux 或默认环境 */
    #include <stdio.h>              /* printf, fprintf, snprintf */
    #include <time.h>               /* time, localtime, strftime */
    #ifdef USE_SYSLOG
        #include <syslog.h>         /* syslog, openlog, closelog */
    #endif
#endif

/* ================================================================== */
/*                    内部状态变量                                    */
/* ================================================================== */

/** @brief 当前日志过滤级别 */
static int g_log_level = AUTORUN_LOG_LEVEL;

/** @brief 当前运行阶段标识 */
static autorun_stage_t g_current_stage = AUTORUN_STAGE_UNKNOWN;

/** @brief 自定义输出回调函数 */
static autorun_log_output_fn_t g_custom_output_fn = NULL;

/** @brief 用户数据指针（传递给回调函数）*/
static void *g_user_data = NULL;

/** @brief 日志系统是否已初始化 */
static bool g_initialized = false;

/* ================================================================== */
/*                    线程安全机制                                    */
/* ================================================================== */

/*
 * 多线程环境的互斥锁实现
 * 单线程环境下定义为空操作以避免开销
 */

#if defined(PLATFORM_RTTHREAD) || defined(PLATFORM_LINUX)
    /* 多线程环境：使用平台提供的互斥锁 */

    #if defined(PLATFORM_RTTHREAD)
        static struct rt_mutex g_log_mutex;
        static bool g_mutex_created = false;

        static void log_lock_init(void) {
            if (!g_mutex_created) {
                rt_mutex_init(&g_log_mutex, "log_mutex", RT_IPC_FLAG_FIFO);
                g_mutex_created = true;
            }
        }

        static void log_lock(void) {
            rt_mutex_take(&g_log_mutex, RT_WAITING_FOREVER);
        }

        static void log_unlock(void) {
            rt_mutex_release(&g_log_mutex);
        }

    #elif defined(PLATFORM_LINUX)
        #include <pthread.h>
        static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

        static void log_lock_init(void) { /* 已在初始化时完成 */ }
        static void log_lock(void)   { pthread_mutex_lock(&g_log_mutex); }
        static void log_unlock(void) { pthread_mutex_unlock(&g_log_mutex); }

    #endif

#else
    /* 单线程环境（BL1/BL31/U-Boot）：空实现 */

    static void log_lock_init(void) {}
    static void log_lock(void)   {}
    static void log_unlock(void) {}

#endif

/* ================================================================== */
/*                    内部辅助函数                                    */
/* ================================================================== */

/**
 * @brief 获取日志级别的字符串表示
 *
 * @param[in] level 日志级别
 * @return const char* 级别名称字符串（大写）
 */
static const char* level_to_string(int level)
{
    switch (level) {
        case AUTORUN_LOG_OFF:     return "OFF";
        case AUTORUN_LOG_ERROR:   return "ERROR";
        case AUTORUN_LOG_WARN:    return "WARN";
        case AUTORUN_LOG_INFO:    return "INFO";
        case AUTORUN_LOG_DEBUG:   return "DEBUG";
        case AUTORUN_LOG_VERBOSE: return "VERBOSE";
        default:                  return "UNKNOWN";
    }
}

/**
 * @brief 获取阶段名称的字符串表示
 *
 * @param[in] stage 阶段标识
 * @return const char* 阶段名称字符串
 */
static const char* stage_to_string(autorun_stage_t stage)
{
    switch (stage) {
        case AUTORUN_STAGE_UNKNOWN:  return "UNKNOWN";
        case AUTORUN_STAGE_BL1:      return "BL1";
        case AUTORUN_STAGE_BL31:     return "BL31";
        case AUTORUN_STAGE_RTTHREAD: return "RTT";
        case AUTORUN_STAGE_UBOOT:    return "UBOOT";
        case AUTORUN_STAGE_LINUX:    return "LINUX";
        case AUTORUN_STAGE_CUSTOM:   return "CUSTOM";
        default:                     return "????";
    }
}

/**
 * @brief 获取 ANSI 颜色码（如果启用）
 *
 * @param[in] level 日志级别
 * @return const char* ANSI 转义序列或空字符串
 */
#ifdef AUTORUN_LOG_COLOR
static const char* get_color_code(int level)
{
    switch (level) {
        case AUTORUN_LOG_ERROR:   return "\033[31m";  /* 红色 */
        case AUTORUN_LOG_WARN:    return "\033[33m";  /* 黄色 */
        case AUTORUN_LOG_INFO:    return "\033[32m";  /* 绿色 */
        case AUTORUN_LOG_DEBUG:   return "\033[36m";  /* 青色 */
        case AUTORUN_LOG_VERBOSE: return "\033[35m";  /* 紫色 */
        default:                  return "\033[0m";   /* 重置 */
    }
}

static const char* get_color_reset(void)
{
    return "\033[0m";
}
#else
static const char* get_color_code(int level)   { (void)level; return ""; }
static const char* get_color_reset(void)        { return ""; }
#endif

/**
 * @brief 格式化时间戳前缀
 *
 * 将当前时间格式化为 [HH:MM:SS.mmm] 形式。
 *
 * @param[out] buf 输出缓冲区
 * @param[in]  buf_size 缓冲区大小
 * @return int 写入的字符数（不含终止符），-1 表示错误
 */
#ifdef AUTORUN_LOG_TIMESTAMP
static int format_timestamp(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) return -1;

#if defined(PLATFORM_LINUX)
    /* Linux: 使用高精度时钟 */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    return snprintf(buf, buf_size,
                   "[%02d:%02d:%02d.%03ld]",
                   tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
                   ts.tv_nsec / 1000000L);

#elif defined(PLATFORM_RTTHREAD)
    /* RT-Thread: 使用 tick 计算 */
    rt_tick_t tick = rt_tick_get();
    long seconds = tick / RT_TICK_PER_SECOND;
    long millis = (tick % RT_TICK_PER_SECOND) * 1000 / RT_TICK_PER_SECOND;

    return snprintf(buf, buf_size,
                   "[%08ld.%03ld]", seconds, millis);

#elif defined(PLATFORM_UBOOT)
    /* U-Boot: 使用 get_timer */
    ulong ms = get_timer(0);
    long seconds = ms / 1000;
    long remainder = ms % 1000;

    return snprintf(buf, buf_size,
                   "[%04ld.%03ld]", seconds, remainder);

#elif defined(PLATFORM_BL1) || defined(PLATFORM_BL31)
    /* ATF: 使用 Generic Timer */
    extern uint64_t read_cntvct_el0(void);
    uint64_t ticks = read_cntvct_el0();
    /* 假设 24MHz 时钟频率 */
    uint64_t us = ticks / 24;
    long seconds = (long)(us / 1000000ULL);
    long millis = (long)((us % 1000000ULL) / 1000ULL);

    return snprintf(buf, buf_size,
                   "[%04ld.%03ld]", seconds, millis);

#else
    /* 默认：无时间戳 */
    return 0;

#endif
}
#else
static int format_timestamp(char *buf, size_t buf_size)
{
    (void)buf; (void)buf_size;
    return 0;
}
#endif

/**
 * @brief 构造完整的日志消息（含所有前缀）
 *
 * 组合时间戳、级别、阶段等信息为最终输出的字符串。
 *
 * @param[out] final_msg    最终消息缓冲区
 * @param[in]  msg_size     缓冲区大小
 * @param[in]  level        日志级别
 * @param[in]  user_msg     用户原始消息
 * @return int 写入的总字符数
 */
static int build_formatted_message(char *final_msg, size_t msg_size,
                                   int level, const char *user_msg)
{
    int offset = 0;
    int written;

    /* 1. 时间戳前缀 */
    if (offset < (int)msg_size) {
        written = format_timestamp(final_msg + offset, msg_size - offset);
        if (written > 0) offset += written;
    }

    /* 2. 级别前缀 */
    if (offset < (int)msg_size) {
        written = snprintf(final_msg + offset, msg_size - offset,
                          " [%s]", level_to_string(level));
        if (written > 0 && offset + written <= (int)msg_size) {
            offset += written;
        }
    }

    /* 3. 阶段标识 */
    if (offset < (int)msg_size) {
        written = snprintf(final_msg + offset, msg_size - offset,
                          " [%s]", stage_to_string(g_current_stage));
        if (written > 0 && offset + written <= (int)msg_size) {
            offset += written;
        }
    }

    /* 4. ANSI 颜色码（如果启用）*/
    if (offset < (int)msg_size) {
        const char *color = get_color_code(level);
        size_t color_len = strlen(color);
        if (color_len > 0 && offset + color_len < msg_size) {
            memcpy(final_msg + offset, color, color_len);
            offset += color_len;
        }
    }

    /* 5. 用户消息 */
    if (offset < (int)msg_size) {
        written = snprintf(final_msg + offset, msg_size - offset,
                          " %s", user_msg);
        if (written > 0 && offset + written <= (int)msg_size) {
            offset += written;
        }
    }

    /* 6. 颜色重置码（如果启用）*/
    if (offset < (int)msg_size) {
        const char *reset = get_color_reset();
        size_t reset_len = strlen(reset);
        if (reset_len > 0 && offset + reset_len < msg_size) {
            memcpy(final_msg + offset, reset, reset_len);
            offset += reset_len;
        }
    }

    /* 确保终止符 */
    if (offset >= (int)msg_size) {
        offset = msg_size - 1;
    }
    final_msg[offset] = '\0';

    return offset;
}

/* ================================================================== */
/*              平台特定输出函数实现                                 */
/* ================================================================== */

/**
 * @brief 底层输出函数 - ATF BL1/BL31
 *
 * 使用 ARM Trusted Firmware 的日志 API。
 */
#if defined(PLATFORM_BL1) || defined(PLATFORM_BL31)
static void platform_output_atf(int level, const char *formatted_msg)
{
    /*
     * ATF 日志级别映射:
     * - LOG_ERROR -> ERROR() 宏
     * - LOG_WARN  -> WARN() 宏
     * - LOG_INFO  -> INFO() 或 NOTICE() 宏
     * - LOG_DEBUG -> INFO() 宏 (ATF 无 DEBUG 级别)
     */

    /* 注意：ATF 的 INFO/WARN/ERROR 宏不接受动态格式化参数，
     *       所以我们直接调用底层的 tf_log 函数 */

    (void)level;  /* ATF 的 tf_log 自行处理级别 */

    /* 直接输出已格式化的消息 */
    tf_log("%s\n", formatted_msg);
}
#endif

/**
 * @brief 底层输出函数 - RT-Thread
 */
#if defined(PLATFORM_RTTHREAD)
static void platform_output_rtthread(int level, const char *formatted_msg)
{
    /*
     * RT-Thread 日志策略:
     * 1. 如果启用了 ulog 统一日志框架 -> 使用 LOG_x 宏
     * 2. 否则 -> 使用 rt_kprintf 直接输出
     */

#ifdef RT_USING_ULOG
    /* 使用 ulog 框架（支持模块化、颜色、级别过滤）*/
    switch (level) {
        case AUTORUN_LOG_ERROR:
            LOG_E("%s", formatted_msg);
            break;
        case AUTORUN_LOG_WARN:
            LOG_W("%s", formatted_msg);
            break;
        case AUTORUN_LOG_INFO:
            LOG_I("%s", formatted_msg);
            break;
        case AUTORUN_LOG_DEBUG:
        case AUTORUN_LOG_VERBOSE:
            LOG_D("%s", formatted_msg);
            break;
        default:
            LOG_I("%s", formatted_msg);
            break;
    }
#else
    /* 回退到 rt_kprintf */
    (void)level;  /* 我们已经处理了级别前缀 */
    rt_kprintf("%s\n", formatted_msg);
#endif
}
#endif

/**
 * @brief 底层输出函数 - U-Boot
 */
#if defined(PLATFORM_UBOOT)
static void platform_output_uboot(int level, const char *formatted_msg)
{
    /*
     * U-Boot 日志策略:
     * 1. ERROR/WARN -> stderr (通过 fprintf(stderr, ...))
     * 2. INFO/DEBUG -> stdout (通过 printf)
     * 3. 如果启用 CONFIG_LOG -> 使用 log() 函数
     */

#ifdef CONFIG_LOG
    /* 使用 U-Boot 的 log 系统 */
    int uboot_level;
    switch (level) {
        case AUTORUN_LOG_ERROR:   uboot_level = LOGL_ERROR;  break;
        case AUTORUN_LOG_WARN:    uboot_level = LOGL_WARNING; break;
        case AUTORUN_LOG_INFO:    uboot_level = LOGL_INFO;    break;
        case AUTORUN_LOG_DEBUG:
        case AUTORUN_LOG_VERBOSE: uboot_level = LOGL_DEBUG;   break;
        default:                  uboot_level = LOGL_INFO;    break;
    }
    log(uboot_level, "%s", formatted_msg);
#else
    /* 标准 I/O 方式 */
    if (level <= AUTORUN_LOG_WARN) {
        /* 错误和警告输出到 stderr */
        fprintf(stderr, "%s\n", formatted_msg);
    } else {
        /* 信息和调试输出到 stdout */
        printf("%s\n", formatted_msg);
    }
#endif
}
#endif

/**
 * @brief 底层输出函数 - Linux
 */
#if defined(PLATFORM_LINUX) || (!defined(PLATFORM_BL1) && \
    !defined(PLATFORM_BL31) && !defined(PLATFORM_RTTHREAD) && \
    !defined(PLATFORM_UBOOT))
static void platform_output_linux(int level, const char *formatted_msg)
{
    /*
     * Linux 日志策略:
     * 1. 如果启用 USE_SYSLOG 且 use_syslog=true -> syslog()
     * 2. 否则 -> printf/fprintf
     */

    /* 检查是否应该使用 syslog */
    extern bool linux_use_syslog(void);  /* 从 linux_adapter 获取配置 */

    if (linux_use_syslog && linux_use_syslog()) {
        /* 使用 syslog 输出 */
        int priority;
        switch (level) {
            case AUTORUN_LOG_ERROR:   priority = LOG_ERR;    break;
            case AUTORUN_LOG_WARN:    priority = LOG_WARNING;break;
            case AUTORUN_LOG_INFO:    priority = LOG_INFO;   break;
            case AUTORUN_LOG_DEBUG:
            case AUTORUN_LOG_VERBOSE: priority = LOG_DEBUG;  break;
            default:                  priority = LOG_INFO;   break;
        }
        syslog(priority, "%s", formatted_msg);
    } else {
        /* 使用标准 I/O */
        if (level <= AUTORUN_LOG_WARN) {
            fprintf(stderr, "%s\n", formatted_msg);
        } else {
            printf("%s\n", formatted_msg);
        }
    }
}
#endif

/**
 * @brief 默认/通用输出函数（无平台特定实现时的回退）
 */
#ifndef platform_output_default
static void platform_output_default(int level, const char *formatted_msg)
{
    /* 最简单的回退方案：使用标准 printf */
    (void)level;
    printf("%s\n", formatted_msg);
}
#endif

/**
 * @brief 选择并调用正确的平台输出函数
 *
 * 根据编译时确定的平台宏选择对应的输出实现。
 */
static void dispatch_to_platform(int level, const char *formatted_msg)
{
#if defined(PLATFORM_BL1) || defined(PLATFORM_BL31)
    platform_output_atf(level, formatted_msg);

#elif defined(PLATFORM_RTTHREAD)
    platform_output_rtthread(level, formatted_msg);

#elif defined(PLATFORM_UBOOT)
    platform_output_uboot(level, formatted_msg);

#elif defined(PLATFORM_LINUX)
    platform_output_linux(level, formatted_msg);

#else
    /* 默认/未知平台 */
    platform_output_default(level, formatted_msg);

#endif
}

/* ================================================================== */
/*                公共API函数实现                                     */
/* ================================================================== */

/**
 * @brief 初始化日志系统
 */
int autorun_log_init(autorun_stage_t stage)
{
    return autorun_log_init_ex(stage, NULL, NULL);
}

/**
 * @brief 初始化日志系统（带自定义回调）
 */
int autorun_log_init_ex(autorun_stage_t stage,
                        autorun_log_output_fn_t output_fn,
                        void *user_data)
{
    /* 参数校验 */
    if (stage < AUTORUN_STAGE_UNKNOWN || stage > AUTORUN_STAGE_CUSTOM) {
        return -1;
    }

    /* 初始化互斥锁 */
    log_lock_init();

    /* 加锁保护初始化过程 */
    log_lock();

    /* 设置内部状态 */
    g_current_stage = stage;
    g_custom_output_fn = output_fn;
    g_user_data = user_data;
    g_initialized = true;

    /* 设置默认日志级别（可被后续 autorun_log_set_level 覆盖）*/
    g_log_level = AUTORUN_LOG_LEVEL;

    /* 平台特定的初始化 */
#if defined(PLATFORM_LINUX) && defined(USE_SYSLOG)
    /* 如果使用 syslog，可以在这里打开 */
    /* openlog("autorun", LOG_PID | LOG_NDELAY, LOG_USER); */
#endif

    log_unlock();

    return 0;
}

/**
 * @brief 关闭日志系统
 */
void autorun_log_deinit(void)
{
    if (!g_initialized) {
        return;
    }

    log_lock();

    /* 清理资源 */
    g_custom_output_fn = NULL;
    g_user_data = NULL;
    g_initialized = false;

    /* 平台特定的清理 */
#if defined(PLATFORM_LINUX) && defined(USE_SYSLOG)
    /* closelog(); */
#endif

    log_unlock();
}

/**
 * @brief 设置全局日志过滤级别
 */
void autorun_log_set_level(int level)
{
    if (level >= AUTORUN_LOG_OFF && level <= AUTORUN_LOG_VERBOSE) {
        log_lock();
        g_log_level = level;
        log_unlock();
    }
}

/**
 * @brief 获取当前日志过滤级别
 */
int autorun_log_get_level(void)
{
    int level;
    log_lock();
    level = g_log_level;
    log_unlock();
    return level;
}

/**
 * @brief 设置当前运行阶段
 */
void autorun_log_set_stage(autorun_stage_t stage)
{
    if (stage >= AUTORUN_STAGE_UNKNOWN && stage <= AUTORUN_STAGE_CUSTOM) {
        log_lock();
        g_current_stage = stage;
        log_unlock();
    }
}

/**
 * @brief 获取当前运行阶段
 */
autorun_stage_t autorun_log_get_stage(void)
{
    autorun_stage_t stage;
    log_lock();
    stage = g_current_stage;
    log_unlock();
    return stage;
}

/**
 * @brief 核心日志输出函数（va_list 版本）
 *
 * 这是所有日志宏最终调用的核心函数。
 * 执行以下步骤:
 * 1. 检查日志级别过滤
 * 2. 构建完整的格式化消息（含前缀）
 * 3. 调用自定义回调或平台输出函数
 *
 * @note 此函数是线程安全的
 */
void autorun_log_output_v(int level, const char *fmt, va_list args)
{
    /* 快速路径：未初始化或级别不够 */
    if (!g_initialized) {
        /* 未初始化时尝试直接输出到平台默认位置 */
        char simple_msg[AUTORUN_LOG_MAX_MSG_LEN];
        vsnprintf(simple_msg, sizeof(simple_msg), fmt, args);
        dispatch_to_platform(level, simple_msg);
        return;
    }

    /* 运行期级别检查 */
    if (level > g_log_level) {
        return;  /* 过滤掉低优先级日志 */
    }

    /* 加锁保护共享资源（缓冲区、状态等）*/
    log_lock();

    /* 格式化用户消息 */
    char user_msg[AUTORUN_LOG_MAX_MSG_LEN];
    vsnprintf(user_msg, sizeof(user_msg), fmt, args);

    /* 构造完整消息（含所有前缀）*/
    char final_msg[AUTORUN_LOG_PREFIX_LEN + AUTORUN_LOG_MAX_MSG_LEN + 64];
    build_formatted_message(final_msg, sizeof(final_msg), level, user_msg);

    /* 输出日志 */
    if (g_custom_output_fn != NULL) {
        /* 使用自定义回调 */
        g_custom_output_fn(level, g_current_stage, final_msg, g_user_data);
    } else {
        /* 使用平台默认输出 */
        dispatch_to_platform(level, final_msg);
    }

    /* 解锁 */
    log_unlock();
}

/**
 * @brief 核心日志输出函数（变参版本）
 *
 * 便捷封装，将 va_list 版本包装为可接受变参的形式。
 */
void autorun_log_output(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    autorun_log_output_v(level, fmt, args);
    va_end(args);
}

/* ================================================================== */
/*                向后兼容接口                                       */
/* ================================================================== */

/*
 * 以下函数用于与现有的 ctx_log() 接口兼容。
 * Core 层的 autorun_core.c 和 task_executor.c 可以继续使用
 * 这些函数，也可以迁移到新的宏接口。
 */

/**
 * @brief 兼容性日志函数（供 core 层使用）
 *
 * 与 ctx_log() 功能相同，但使用统一的日志系统。
 * 可选：core 层可以选择使用此函数或直接使用 AUTORUN_LOG_xxx 宏。
 *
 * @param[in] ctx   框架上下文 (可为NULL，此时使用全局设置)
 * @param[in] level 日志级别
 * @param[in] fmt   格式字符串
 * @param[in] ...   可变参数
 */
void autorun_ctx_log(void *ctx, int level, const char *fmt, ...)
{
    (void)ctx;  /* 当前不使用 ctx，未来可用于上下文相关的日志配置 */

    va_list args;
    va_start(args, fmt);
    autorun_log_output_v(level, fmt, args);
    va_end(args);
}

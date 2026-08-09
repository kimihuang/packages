/**
 * @file rtthread_adapter.c
 * @brief RT-Thread 平台适配层实现
 *
 * 本文件实现了 RT-Thread 操作系统的平台适配，
 * 将 RT-Thread 提供的底层能力映射到自动运行框架的标准接口。
 *
 * 实现要点:
 * 1. **日志适配**: rt_kprintf() / LOG_I/LOG_E/LOG_W → 框架的 log_printf()
 * 2. **命令执行**: 调用 finsh/msh 命令执行接口
 * 3. **时间管理**: rt_tick_get() + TICK_PER_SECOND
 * 4. **延时管理**: rt_thread_mdelay()
 * 5. **内存操作**: 使用标准 libc 或 RT-Thread 的内存函数
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "rtthread_adapter.h"
#include "../core/autorun_core.h"

/*
 * RT-Thread 头文件包含
 */
#include <rtthread.h>              /* RT-Thread 内核 API */
#include <rtdevice.h>             /* 设备驱动框架 (可选) */
#ifdef RT_USING_FINSH
    #include <finsh.h>            /* finsh shell 接口 */
#endif
#ifdef RT_USING_DFS
    #include <dfs_posix.h>        /* 文件系统接口 (可选) */
#endif

/* ================================================================== */
/*                    内部常量定义                                    */
/* ================================================================== */

/** @brief 输出缓冲区大小 (栈上分配)
 *
 * RT-Thread 栈空间相对充足，可使用较大的缓冲区。
 */
#define OUTPUT_BUFFER_SIZE          1024

/** @brief 命令执行超时时的轮询间隔 (ms) */
#define EXEC_POLL_INTERVAL_MS       10

/** @brief 默认命令执行超时时间 (ms) */
#define DEFAULT_EXEC_TIMEOUT_MS     30000

/* ================================================================== */
/*                    全局状态变量                                    */
/* ================================================================== */

/** @brief 最后一次执行结果 */
static int g_last_result = 0;

/** @brief 执行完成标记 */
static bool g_completed = false;

/** @brief 平台操作函数表 (单例) */
static autorun_platform_ops_t g_rtthread_ops;

/** @brief 操作表是否已初始化 */
static bool g_ops_initialized = false;

/* ================================================================== */
/*                 内部辅助函数声明                                   */
/* ================================================================== */

static void init_platform_ops(void);
static int map_log_level(int framework_level);
static uint32_t tick_to_ms(rt_tick_t tick);
static rt_tick_t ms_to_tick(uint32_t ms);

/* ================================================================== */
/*               内部辅助函数实现                                      */
/* ================================================================== */

/**
 * @brief 初始化平台操作函数表
 *
 * 将所有平台相关的函数指针映射到RT-Thread的具体实现。
 * 此函数保证只初始化一次（单例模式）。
 */
static void init_platform_ops(void)
{
    if (g_ops_initialized) {
        return;
    }

    g_rtthread_ops.log_printf = rtthread_log_printf;
    g_rtthread_ops.execute_command = rtthread_execute_command;
    g_rtthread_ops.get_timestamp = rtthread_get_timestamp;
    g_rtthread_ops.delay_ms = rtthread_delay_ms;
    g_rtthread_ops.memcpy = memcpy;
    g_rtthread_ops.memset = memset;

    g_ops_initialized = true;
}

/**
 * @brief 将框架日志级别映射到RT-Thread日志级别
 *
 * 框架级别定义:
 *   0: OFF, 1: ERROR, 2: WARN, 3: INFO, 4: DEBUG, 5: VERBOSE
 *
 * RT-Thread LOG宏:
 *   LOG_E - Error
 *   LOG_W - Warning
 *   LOG_I - Info
 *   LOG_D - Debug
 *
 * @param[in] framework_level 框架日志级别 (0-5)
 * @return int 映射后的日志级别标识
 */
static int map_log_level(int framework_level)
{
    switch (framework_level) {
        case 0: return -1;  /* OFF - 不输出 */
        case 1: return 0;   /* ERROR */
        case 2: return 1;   /* WARNING */
        case 3: return 2;   /* INFO */
        case 4: return 3;   /* DEBUG */
        case 5: return 3;   /* VERBOSE -> DEBUG */
        default: return 2;  /* 默认INFO */
    }
}

/**
 * @brief 将 RT-Thread tick 转换为毫秒
 *
 * @param[in] tick 系统时钟滴答数
 * @return uint32_t 对应的毫秒数
 */
static uint32_t tick_to_ms(rt_tick_t tick)
{
#if defined(RT_TICK_PER_SECOND) && (RT_TICK_PER_SECOND != 0)
    return (uint32_t)((tick * 1000UL) / RT_TICK_PER_SECOND);
#elif (RTTHREAD_TICK_PER_SECOND != 0)
    return (uint32_t)((tick * 1000UL) / RTTHREAD_TICK_PER_SECOND);
#else
    /* 假设默认 1000 ticks/sec = 1ms/tick */
    return (uint32_t)tick;
#endif
}

/**
 * @brief 将毫秒转换为 RT-Thread tick
 *
 * @param[in] ms 毫秒数
 * @return rt_tick_t 对应的系统时钟滴答数
 */
static rt_tick_t ms_to_tick(uint32_t ms)
{
#if defined(RT_TICK_PER_SECOND) && (RT_TICK_PER_SECOND != 0)
    return (rt_tick_t)((ms * RT_TICK_PER_SECOND) / 1000UL);
#elif (RTTHREAD_TICK_PER_SECOND != 0)
    return (rt_tick_t)((ms * RTTHREAD_TICK_PER_SECOND) / 1000UL);
#else
    return (rt_tick_t)ms;
#endif
}

/* ================================================================== */
/*              平台操作函数实现 (供框架调用)                          */
/* ================================================================== */

/**
 * @brief 日志输出函数
 *
 * 将框架的日志请求映射到RT-Thread的日志输出系统。
 * 支持两种模式:
 * 1. 使用 LOG_x 宏 (推荐，支持组件化日志和颜色输出)
 * 2. 使用 rt_kprintf (兼容性更好，无额外依赖)
 *
 * @param[in] level 日志级别 (0-5)
 * @param[in] fmt 格式字符串
 * @param[in] ... 可变参数列表
 */
void rtthread_log_printf(int level, const char *fmt, ...)
{
    int rt_level = map_log_level(level);

    /* 级别为 OFF (-1) 时不输出 */
    if (rt_level < 0) {
        return;
    }

    va_list args;
    va_start(args, fmt);

/*
 * 根据可用的日志接口选择输出方式
 * 优先级: LOG_x 宏 > rt_kprintf
 */
#ifdef RT_USING_ULOG
    /* 使用 ulog 统一日志框架 (RT-Thread 3.4+) */
    {
        char log_buf[256];
        rt_vsnprintf(log_buf, sizeof(log_buf), fmt, args);

        switch (rt_level) {
            case 0:  /* ERROR */
                log_e("%s", log_buf);
                break;
            case 1:  /* WARNING */
                log_w("%s", log_buf);
                break;
            case 2:  /* INFO */
                log_i("%s", log_buf);
                break;
            case 3:  /* DEBUG */
            default:
                log_d("%s", log_buf);
                break;
        }
    }
#else
    /* 使用 rt_kprintf 直接输出 */
    {
        /* 可选：添加级别前缀 */
        const char *level_prefix[] = {
            "",           /* 0: OFF (不会到达) */
            "[ERROR] ",   /* 1: ERROR */
            "[WARN]  ",   /* 2: WARNING */
            "[INFO] ",    /* 3: INFO */
            "[DEBUG] ",   /* 4: DEBUG */
            "[DEBUG] "    /* 5: VERBOSE */
        };

        if (rt_level >= 0 && rt_level <= 5) {
            rt_kprintf("%s", level_prefix[rt_level]);
        }

        rt_vprintf(fmt, args);
    }
#endif

    va_end(args);
}

/**
 * @brief Shell 命令执行函数
 *
 * 在 RT-Thread 环境中执行 Shell 命令并捕获输出。
 *
 * 实现策略（按优先级）:
 * 1. 使用 finsh_exec_command() 或 msh_exec() (如果可用)
 * 2. 使用自定义的命令解析和执行逻辑
 * 3. 返回"未实现"错误信息
 *
 * @param[in] command     要执行的命令字符串
 * @param[out] output     输出缓冲区，存储命令的标准输出
 * @param[in] output_size 输出缓冲区大小 (bytes)
 * @param[in] timeout_ms  超时时间 (毫秒)，0表示无限等待
 * @return int 命令返回码 (0=成功, 非0=失败)
 */
int rtthread_execute_command(const char *command,
                             char *output,
                             size_t output_size,
                             uint32_t timeout_ms)
{
    /*
     * 命令执行函数。
     *
     * 在RT-Thread环境中，此函数应调用Shell命令执行接口。
     * RT-Thread提供多种方式执行命令:
     *
     * 方式1: 使用 finsh/msh 的内部执行函数
     *   #ifdef RT_USING_FINSH
     *       extern int finsh_exec_command(const char *cmd, ...);
     *       ret = finsh_exec_command(command, output, output_size);
     *   #endif
     *
     * 方式2: 创建临时线程执行命令并捕获输出
     *   - 将命令作为参数传递给msh线程
     *   - 通过管道或共享缓冲区获取输出
     *
     * 方式3: 直接调用已注册的命令函数
     *   - 遍历 finsh_syscall 表查找命令
     *   - 直接调用对应的函数指针
     *
     * 如果平台未提供具体实现，此处返回错误。
     * 使用者可通过以下方式之一提供命令执行能力:
     * 1. 在平台代码中实现 plat_execute_command() 并在此调用
     * 2. 或在初始化前替换 g_rtthread_ops.execute_command 函数指针
     */

    /* 参数校验 */
    if (command == NULL || command[0] == '\0') {
        if (output != NULL && output_size > 0) {
            output[0] = '\0';
        }
        return -1;
    }

    /*
     * TODO: 实现具体的命令执行逻辑
     *
     * 以下是一个参考实现框架，需要根据实际环境完善:
     */

#ifdef RT_USING_FINSH
    /*
     * 尝试使用 finsh/msh 执行命令
     * 注意：这需要访问 finsh 的内部API或自定义封装
     */
    extern int msh_exec(const char *cmd, int size);

    /* 重定向输出到缓冲区 (如果支持) */
    /* ... 实现输出捕获逻辑 ... */

    int ret = msh_exec(command, strlen(command));

    if (ret == 0) {
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "Command executed successfully: %.96s", command);
        }
        return 0;
    } else {
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "Command failed with code %d: %.80s", ret, command);
        }
        return ret;
    }
#else
    /* 无 finsh 支持，提示用户实现 */
    if (output != NULL && output_size > 0) {
        snprintf(output, output_size,
                "Command execution not implemented in RT-Thread adapter: %.48s",
                command);
    }

    rtthread_log_printf(1,
            "RT-Thread Adapter: execute_command() not fully implemented\n");
    rtthread_log_printf(1,
            "  Platform must provide command execution via:\n");
    rtthread_log_printf(1,
            "  1. Implementing plat_execute_command() in platform code\n");
    rtthread_log_printf(1,
            "  2. Or replacing ops->execute_command before calling rtthread_run_autorun()\n");
    rtthread_log_printf(1,
            "  3. Enable RT_USING_FINSH in rtconfig.h for basic support\n");

    return -1;
#endif
}

/**
 * @brief 获取当前系统时间戳 (毫秒)
 *
 * 基于 RT-Thread 的系统时钟滴答计算从启动以来的毫秒数。
 *
 * @return uint32_t 当前时间戳 (毫秒)
 */
uint32_t rtthread_get_timestamp(void)
{
    return tick_to_ms(rt_tick_get());
}

/**
 * @brief 毫秒级延时函数
 *
 * 使用 RT-Thread 的延时 API 进行精确延时。
 * 相比忙等待，这种方式更节能且不影响其他线程调度。
 *
 * @param[in] ms 延时毫秒数
 */
void rtthread_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    /*
     * 优先使用 rt_thread_mdelay() (毫秒级延时)
     * 如果不可用则回退到 rt_thread_delay() (tick级延时)
     */
#ifdef RT_USING_MDELAY
    rt_thread_mdelay(ms);
#else
    rt_thread_delay(ms_to_tick(ms));
#endif
}

/* ================================================================== */
/*                核心API函数实现                                     */
/* ================================================================== */

/**
 * @brief 使用默认配置地址运行自动任务
 *
 * @return int 0=所有任务通过, 非0=有失败或错误
 */
int rtthread_run_autorun(void)
{
    return rtthread_run_autorun_with_addr(
        PLAT_RTTHREAD_AUTORUN_CONFIG_ADDR,
        PLAT_RTTHREAD_AUTORUN_RESULT_ADDR,
        PLAT_RTTHREAD_AUTORUN_RESULT_SIZE
    );
}

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * 这是RT-Thread适配层的主要入口函数，完成完整的自动运行流程：
 * 1. 打印启动信息
 * 2. 初始化平台操作函数表
 * 3. 初始化框架核心引擎
 * 4. 配置运行选项
 * 5. 执行 'rtthread' 阶段的所有任务
 * 6. 收集并打印结果摘要
 * 7. 清理资源
 *
 * @param[in] config_addr 配置数据的物理地址
 * @param[in] result_addr 结果区域的物理地址
 * @param[in] result_size 结果区域大小 (bytes)
 * @return int 0=成功, 非0=错误
 */
int rtthread_run_autorun_with_addr(uint32_t config_addr,
                                   uint32_t result_addr,
                                   uint32_t result_size)
{
    rtthread_log_printf(3, "\n");
    rtthread_log_printf(3, "========================================\n");
    rtthread_log_printf(3, "  RT-Thread AutoRun Framework Starting\n");
    rtthread_log_printf(3, "========================================\n");
    rtthread_log_printf(3, "Config addr:  0x%08lX\n", (unsigned long)config_addr);
    rtthread_log_printf(3, "Result addr: 0x%08lX\n", (unsigned long)result_addr);
    rtthread_log_printf(3, "Result size: %u bytes\n", result_size);
    rtthread_log_printf(3, "\n");

    /* 初始化平台操作函数表 */
    init_platform_ops();

    /* 初始化框架核心引擎 */
    autorun_context_t *ctx = NULL;
    int ret = autorun_init(config_addr,
                           result_addr,
                           result_size,
                           &g_rtthread_ops,
                           &ctx);

    if (ret != 0) {
        rtthread_log_printf(1, "RT-Thread AutoRun: Init failed with error %d\n", ret);

        switch (ret) {
            case AUTORUN_ERR_INVALID_MAGIC:
                rtthread_log_printf(1, "  -> Invalid magic number\n");
                break;
            case AUTORUN_ERR_VERSION_MISMATCH:
                rtthread_log_printf(1, "  -> Version mismatch\n");
                break;
            case AUTORUN_ERR_CRC_ERROR:
                rtthread_log_printf(1, "  -> CRC32 check failed\n");
                break;
            case AUTORUN_ERR_INVALID_PARAM:
                rtthread_log_printf(1, "  -> Invalid parameter\n");
                break;
            case AUTORUN_ERR_NO_MEMORY:
                rtthread_log_printf(1, "  -> Result buffer too small\n");
                break;
            default:
                rtthread_log_printf(1, "  -> Unknown error code\n");
                break;
        }

        g_last_result = ret;
        g_completed = true;
        return ret;
    }

    rtthread_log_printf(3, "RT-Thread AutoRun: Framework initialized successfully\n");

    /* 配置运行选项 */
    autorun_options_t options;
    memset(&options, 0, sizeof(options));

    options.stop_on_critical_fail = true;
    options.capture_full_output = true;      /* RT-Thread 内存充足，启用完整输出记录 */
    options.global_timeout_ms = 180000;      /* 3分钟全局超时 */
    options.verbose_logging = true;          /* 启用详细日志 */

    autorun_set_options(ctx, &options);

    /* 执行 RT-Thread 阶段的所有任务 */
    rtthread_log_printf(3, "RT-Thread AutoRun: Executing stage 'rtthread'...\n");
    ret = autorun_run_stage(ctx, "rtthread");

    /* 获取并打印结果摘要 */
    const autorun_result_t *result = autorun_get_result(ctx);
    if (result != NULL) {
        rtthread_log_printf(3, "\n");
        rtthread_log_printf(3, "========== RT-Thread AutoRun Result ==========\n");
        rtthread_log_printf(3, "Total tasks:  %u\n", result->total_tasks);
        rtthread_log_printf(3, "Passed:       %u\n", result->passed_count);
        rtthread_log_printf(3, "Failed:       %u\n", result->failed_count);
        rtthread_log_printf(3, "Skipped:      %u\n", result->skipped_count);
        rtthread_log_printf(3, "Elapsed time: %u ms\n", result->total_elapsed_ms);
        rtthread_log_printf(3, "Overall:      %s\n",
               (result->overall_status == 0) ? "PASS" : "FAIL");

        if (result->failed_count > 0 &&
            result->first_failed_task[0] != '\0') {
            rtthread_log_printf(2, "First failure: %s (ID=%u)\n",
                 result->first_failed_task,
                 result->first_failed_task_id);
        }
        rtthread_log_printf(3, "================================================\n\n");
    } else {
        rtthread_log_printf(2, "RT-Thread AutoRun: No result available\n");
    }

    /* 清理框架资源 */
    autorun_cleanup(ctx);

    /* 更新状态 */
    g_last_result = ret;
    g_completed = true;

    rtthread_log_printf(3, "RT-Thread AutoRun: Completed with code %d\n", ret);
    rtthread_log_printf(3, "========================================\n\n");

    return ret;
}

/**
 * @brief 获取RT-Thread适配器的平台操作表
 *
 * 允许外部模块在初始化前修改操作表中的特定函数指针，
 * 以实现自定义的平台行为。
 *
 * @return const autorun_platform_ops_t* 平台操作表指针 (静态存储)
 */
const autorun_platform_ops_t* rtthread_get_platform_ops(void)
{
    init_platform_ops();
    return &g_rtthread_ops;
}

/* ================================================================== */
/*                    状态查询实现                                    */
/* ================================================================== */

/**
 * @brief 检查RT-Thread自动运行是否已完成
 *
 * @return bool true=已完成, false=未执行或正在执行
 */
bool rtthread_is_autorun_completed(void)
{
    return g_completed;
}

/**
 * @brief 获取最后一次执行的返回码
 *
 * @return int 最后一次 rtthread_run_autorun() 的返回值
 */
int rtthread_get_last_result(void)
{
    return g_last_result;
}

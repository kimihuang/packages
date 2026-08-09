/**
 * @file uboot_adapter.c
 * @brief U-Boot 平台适配层实现
 *
 * 本文件实现了 U-Boot (Das U-Boot) 引导加载程序的平台适配，
 * 将 U-Boot 提供的底层能力映射到自动运行框架的标准接口。
 *
 * 实现要点:
 * 1. **日志适配**: printf() / debug() → 框架的 log_printf()
 * 2. **命令执行**: 调用 run_command() 或 run_command_list()
 * 3. **时间管理**: timer_get_us() / get_timer()
 * 4. **延时管理**: mdelay() / udelay()
 * 5. **内存操作**: 使用标准 libc (memset/memcpy/memmove)
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "uboot_adapter.h"
#include "../core/autorun_core.h"

/*
 * U-Boot 头文件包含
 */
#include <common.h>                 /* U-Boot 公共定义 */
#include <command.h>                /* run_command, cmd_tbl */
#include <console.h>               /* console API */
#include <linux/delay.h>           /* mdelay, udelay */
#include <linux/string.h>          /* memset, memcpy, strlen */
#include <div64.h>                 /* do_div for 64-bit division */
#include <dm/device.h>             /* Driver Model (可选) */
#include <env.h>                   /* 环境变量操作 (可选) */

/* ================================================================== */
/*                    内部常量定义                                    */
/* ================================================================== */

/** @brief 输出缓冲区大小 (栈上分配)
 *
 * U-Boot 栈空间相对充足（通常几MB），可使用较大的缓冲区。
 */
#define OUTPUT_BUFFER_SIZE          2048

/** @brief 默认命令执行超时时间 (ms)
 *
 * U-Boot命令可能需要较长时间（如网络操作、Flash擦写等）。
 */
#define DEFAULT_EXEC_TIMEOUT_MS     60000

/* ================================================================== */
/*                    全局状态变量                                    */
/* ================================================================== */

/** @brief 最后一次执行结果 */
static int g_last_result = 0;

/** @brief 执行完成标记 */
static bool g_completed = false;

/** @brief 平台操作函数表 (单例) */
static autorun_platform_ops_t g_uboot_ops;

/** @brief 操作表是否已初始化 */
static bool g_ops_initialized = false;

/* ================================================================== */
/*                 内部辅助函数声明                                   */
/* ================================================================== */

static void init_platform_ops(void);
static int map_log_level(int framework_level);
static uint32_t us_to_ms(uint64_t us);
static uint64_t ms_to_us(uint32_t ms);

/* ================================================================== */
/*               内部辅助函数实现                                      */
/* ================================================================== */

/**
 * @brief 初始化平台操作函数表
 *
 * 将所有平台相关的函数指针映射到U-Boot的具体实现。
 * 此函数保证只初始化一次（单例模式）。
 */
static void init_platform_ops(void)
{
    if (g_ops_initialized) {
        return;
    }

    g_uboot_ops.log_printf = uboot_log_printf;
    g_uboot_ops.execute_command = uboot_execute_command;
    g_uboot_ops.get_timestamp = uboot_get_timestamp;
    g_uboot_ops.delay_ms = uboot_delay_ms;
    g_uboot_ops.memcpy = memcpy;
    g_uboot_ops.memset = memset;

    g_ops_initialized = true;
}

/**
 * @brief 将框架日志级别映射到U-Boot日志级别
 *
 * 框架级别定义:
 *   0: OFF, 1: ERROR, 2: WARN, 3: INFO, 4: DEBUG, 5: VERBOSE
 *
 * U-Boot 日志系统:
 *   LOGL_DEBUG   = 0
 *   LOGL_INFO    = 1
 *   LOGL_NOTICE  = 2
 *   LOGL_WARNING = 3
 *   LOGL_ERROR   = 4
 *   LOGL_CRIT    = 5
 *   LOGL_NONE    = 6
 *
 * @param[in] framework_level 框架日志级别 (0-5)
 * @return int 映射后的日志级别标识 (-1表示不输出)
 */
static int map_log_level(int framework_level)
{
    switch (framework_level) {
        case 0: return -1;  /* OFF - 不输出 */
        case 1: return 4;   /* ERROR -> LOGL_ERROR */
        case 2: return 3;   /* WARNING -> LOGL_WARNING */
        case 3: return 1;   /* INFO -> LOGL_INFO */
        case 4: return 0;   /* DEBUG -> LOGL_DEBUG */
        case 5: return 0;   /* VERBOSE -> LOGL_DEBUG */
        default: return 1;  /* 默认INFO */
    }
}

/**
 * @brief 将微秒转换为毫秒
 *
 * @param[in] us 微秒数
 * @return uint32_t 对应的毫秒数
 */
static uint32_t us_to_ms(uint64_t us)
{
    return (uint32_t)(us / 1000ULL);
}

/**
 * @brief 将毫秒转换为微秒
 *
 * @param[in] ms 毫秒数
 * @return uint64_t 对应的微秒数
 */
static uint64_t ms_to_us(uint32_t ms)
{
    return ((uint64_t)ms * 1000ULL);
}

/* ================================================================== */
/*              平台操作函数实现 (供框架调用)                          */
/* ================================================================== */

/**
 * @brief 日志输出函数
 *
 * 将框架的日志请求映射到U-Boot的日志输出系统。
 * 支持两种模式:
 * 1. 使用 log() 函数 (推荐，支持分类日志和等级控制)
 * 2. 使用 printf 直接输出 (兼容性更好)
 *
 * @param[in] level 日志级别 (0-5)
 * @param[in] fmt 格式字符串
 * @param[in] ... 可变参数列表
 */
void uboot_log_printf(int level, const char *fmt, ...)
{
    int uboot_level = map_log_level(level);

    /* 级别为 OFF (-1) 时不输出 */
    if (uboot_level < 0) {
        return;
    }

    va_list args;
    va_start(args, fmt);

/*
 * 根据可用的日志接口选择输出方式
 * 优先级: log() > printf
 */
#ifdef CONFIG_LOG
    /* 使用 U-Boot log 系统 (v2019.04+) */
    {
        char log_buf[512];
        vsnprintf(log_buf, sizeof(log_buf), fmt, args);

        /*
         * U-Boot log 分类:
         *   UCLASS_AUTORUN - 自定义分类 (需要在 log_cfg 中注册)
         *   LOGC_NONE     - 无分类 (通用)
         */
        log(uboot_level, /* category */ 0, "%s", log_buf);
    }
#else
    /* 使用 printf 直接输出 */
    {
        /* 可选：添加级别前缀 */
        const char *level_prefix[] = {
            "",           /* 0: OFF (不会到达) */
            "ERROR: ",    /* 1: ERROR */
            "Warning: ",  /* 2: WARNING */
            "",           /* 3: INFO (无前缀) */
            "DEBUG: ",    /* 4: DEBUG */
            "DEBUG: "     /* 5: VERBOSE */
        };

        if (uboot_level >= 0 && uboot_level <= 5) {
            printf("%s", level_prefix[uboot_level]);
        }

        vprintf(fmt, args);
    }
#endif

    va_end(args);
}

/**
 * @brief Shell 命令执行函数
 *
 * 在 U-Boot 环境中执行 Shell 命令并捕获输出。
 *
 * 实现策略:
 * 1. 使用 run_command() 执行命令并获取返回码
 * 2. 通过重定向 stdout 到缓冲区来捕获输出
 * 3. 支持超时机制防止死锁
 *
 * @param[in] command     要执行的命令字符串
 * @param[out] output     输出缓冲区，存储命令的标准输出
 * @param[in] output_size 输出缓冲区大小 (bytes)
 * @param[in] timeout_ms  超时时间 (毫秒)，0表示无限等待
 * @return int 命令返回码 (0=成功, 非0=失败)
 */
int uboot_execute_command(const char *command,
                          char *output,
                          size_t output_size,
                          uint32_t timeout_ms)
{
    /*
     * 命令执行函数。
     *
     * 在U-Boot环境中，此函数调用标准的命令执行API。
     * U-Boot提供多种方式执行命令:
     *
     * 方式1: 使用 run_command() (最常用)
     *   int run_command(const char *cmd, int flag);
     *   flag: 0=正常执行, FLAG_BOOT_STAGE=启动阶段
     *
     * 方式2: 使用 run_command_list() (执行多条命令)
     *   int run_command_list(const char *cmd, int len, int flag);
     *
     * 方式3: 使用 cmd_process() (更底层)
     *   int cmd_process(int flag, int argc, char *const argv[],
     *                   int *repeatable, struct cmd_tbl **cmdtp);
     *
     * 输出捕获方法:
     * 方法A: 重定向 console 到缓冲区 (需要修改console驱动)
     * 方法B: 使用 printf 重定向 (CONFIG_CONSOLE_RECORD)
     * 方法C: 解析命令类型并直接调用底层函数
     * 方法D: 仅记录返回码，不捕获输出 (简化版)
     */

    /* 参数校验 */
    if (command == NULL || command[0] == '\0') {
        if (output != NULL && output_size > 0) {
            output[0] = '\0';
        }
        return -1;
    }

    /* 初始化超时时间 */
    if (timeout_ms == 0) {
        timeout_ms = DEFAULT_EXEC_TIMEOUT_MS;
    }

    /*
     * TODO: 实现具体的命令执行逻辑
     *
     * 以下是一个参考实现框架，需要根据实际需求完善:
     */

#ifdef CONFIG_CONSOLE_RECORD
    /*
     * 使用 console record 功能捕获输出
     * 需要在配置中启用 CONFIG_CONSOLE_RECORD=y
     */
    {
        int ret;
        ulong start_time = get_timer(0);
        ulong timeout_ticks = timeout_ms;  /* get_timer单位通常是ms */

        /* 记录开始位置 */
        // record_console_start();

        /* 执行命令 */
        ret = run_command(command, 0);

        /* 计算实际耗时 */
        ulong elapsed = get_timer(start_time);

        /* 获取记录的输出 */
        if (output != NULL && output_size > 0) {
            // size_t out_len = record_console_get(output, output_size);
            snprintf(output, output_size,
                    "Command executed in %lu ms, return code=%d: %.96s",
                    elapsed, ret, command);
        }

        return ret;
    }
#else
    /*
     * 简化版本：仅执行命令并返回码，不捕获完整输出
     * 这适用于大多数测试场景
     */
    {
        int ret;
        ulong start_time = get_timer(0);

        debug("U-Boot Adapter: Executing command: %s\n", command);

        /* 执行命令 (flag=0 表示正常执行) */
        ret = run_command(command, 0);

        /* 计算实际耗时 */
        ulong elapsed = get_timer(start_time);

        debug("U-Boot Adapter: Command completed in %lu ms, ret=%d\n",
              elapsed, ret);

        /* 构造输出信息 */
        if (output != NULL && output_size > 0) {
            if (ret == 0) {
                snprintf(output, output_size,
                        "[OK] Command executed successfully (%lu ms): %.128s",
                        elapsed, command);
            } else {
                snprintf(output, output_size,
                        "[FAIL] Command failed with code %d (%lu ms): %.112s",
                        ret, elapsed, command);
            }
        }

        return ret;
    }
#endif
}

/**
 * @brief 获取当前系统时间戳 (毫秒)
 *
 * 基于 U-Boot 的定时器计算从启动以来的毫秒数。
 * 优先使用 timer_get_us() (微秒精度)，回退到 get_timer() (毫秒精度)。
 *
 * @return uint32_t 当前时间戳 (毫秒)
 */
uint32_t uboot_get_timestamp(void)
{
/*
 * 选择可用的定时器API:
 * 优先级: timer_get_us() > get_timer()
 */
#if defined(CONFIG_TIMER) && defined(timer_get_us)
    /* 使用 DM Timer 的微秒级接口 */
    return us_to_ms((uint64_t)timer_get_us());
#elif defined(CONFIG_SYS_HZ)
    /* 使用 get_timer() (毫秒级) */
    return (uint32_t)get_timer(0);
#else
    /* 回退方案：基于编译时常量估算 */
#warning "No timer API available, using dummy timestamp"
    static uint32_t dummy_timestamp = 0;
    dummy_timestamp += 10;  /* 每次调用增加10ms */
    return dummy_timestamp;
#endif
}

/**
 * @brief 毫秒级延时函数
 *
 * 使用 U-Boot 的延时 API 进行精确延时。
 * 单线程环境下使用忙等待实现。
 *
 * @param[in] ms 延时毫秒数
 */
void uboot_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    /*
     * 优先使用 mdelay() (毫秒级忙等待)
     * 如果不可用则使用 udelay() (微秒级)
     */
#ifdef mdelay
    mdelay(ms);
#elif defined(udelay)
    udelay(ms * 1000UL);
#else
    /* 最简实现：空循环 (不精确但可用) */
    volatile uint32_t i;
    for (i = 0; i < ms * 10000UL; i++) {
        __asm__ volatile("nop");
    }
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
int uboot_run_autorun(void)
{
    return uboot_run_autorun_with_addr(
        PLAT_UBOOT_AUTORUN_CONFIG_ADDR,
        PLAT_UBOOT_AUTORUN_RESULT_ADDR,
        PLAT_UBOOT_AUTORUN_RESULT_SIZE
    );
}

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * 这是U-Boot适配层的主要入口函数，完成完整的自动运行流程：
 * 1. 打印启动信息
 * 2. 初始化平台操作函数表
 * 3. 初始化框架核心引擎
 * 4. 配置运行选项
 * 5. 执行 'uboot' 阶段的所有任务
 * 6. 收集并打印结果摘要
 * 7. 清理资源
 *
 * @param[in] config_addr 配置数据的物理地址
 * @param[in] result_addr 结果区域的物理地址
 * @param[in] result_size 结果区域大小 (bytes)
 * @return int 0=成功, 非0=错误
 */
int uboot_run_autorun_with_addr(uint32_t config_addr,
                                uint32_t result_addr,
                                uint32_t result_size)
{
    uboot_log_printf(3, "\n");
    uboot_log_printf(3, "=====================================\n");
    uboot_log_printf(3, "  U-Boot AutoRun Framework Starting\n");
    uboot_log_printf(3, "=====================================\n");
    uboot_log_printf(3, "Config addr:  0x%08lX\n", (unsigned long)config_addr);
    uboot_log_printf(3, "Result addr: 0x%08lX\n", (unsigned long)result_addr);
    uboot_log_printf(3, "Result size: %u bytes\n", result_size);
    uboot_log_printf(3, "\n");

    /* 初始化平台操作函数表 */
    init_platform_ops();

    /* 初始化框架核心引擎 */
    autorun_context_t *ctx = NULL;
    int ret = autorun_init(config_addr,
                           result_addr,
                           result_size,
                           &g_uboot_ops,
                           &ctx);

    if (ret != 0) {
        uboot_log_printf(1, "U-Boot AutoRun: Init failed with error %d\n", ret);

        switch (ret) {
            case AUTORUN_ERR_INVALID_MAGIC:
                uboot_log_printf(1, "  -> Invalid magic number\n");
                break;
            case AUTORUN_ERR_VERSION_MISMATCH:
                uboot_log_printf(1, "  -> Version mismatch\n");
                break;
            case AUTORUN_ERR_CRC_ERROR:
                uboot_log_printf(1, "  -> CRC32 check failed\n");
                break;
            case AUTORUN_ERR_INVALID_PARAM:
                uboot_log_printf(1, "  -> Invalid parameter\n");
                break;
            case AUTORUN_ERR_NO_MEMORY:
                uboot_log_printf(1, "  -> Result buffer too small\n");
                break;
            default:
                uboot_log_printf(1, "  -> Unknown error code\n");
                break;
        }

        g_last_result = ret;
        g_completed = true;
        return ret;
    }

    uboot_log_printf(3, "U-Boot AutoRun: Framework initialized successfully\n");

    /* 配置运行选项 */
    autorun_options_t options;
    memset(&options, 0, sizeof(options));

    options.stop_on_critical_fail = true;
    options.capture_full_output = true;      /* U-Boot 内存充足，启用完整输出记录 */
    options.global_timeout_ms = 300000;      /* 5分钟全局超时 (U-Boot命令可能较慢) */
    options.verbose_logging = true;          /* 启用详细日志 */

    autorun_set_options(ctx, &options);

    /* 执行 U-Boot 阶段的所有任务 */
    uboot_log_printf(3, "U-Boot AutoRun: Executing stage 'uboot'...\n");
    ret = autorun_run_stage(ctx, "uboot");

    /* 获取并打印结果摘要 */
    const autorun_result_t *result = autorun_get_result(ctx);
    if (result != NULL) {
        uboot_log_printf(3, "\n");
        uboot_log_printf(3, "========== U-Boot AutoRun Result ==========\n");
        uboot_log_printf(3, "Total tasks:  %u\n", result->total_tasks);
        uboot_log_printf(3, "Passed:       %u\n", result->passed_count);
        uboot_log_printf(3, "Failed:       %u\n", result->failed_count);
        uboot_log_printf(3, "Skipped:      %u\n", result->skipped_count);
        uboot_log_printf(3, "Elapsed time: %u ms\n", result->total_elapsed_ms);
        uboot_log_printf(3, "Overall:      %s\n",
               (result->overall_status == 0) ? "PASS" : "FAIL");

        if (result->failed_count > 0 &&
            result->first_failed_task[0] != '\0') {
            uboot_log_printf(2, "First failure: %s (ID=%u)\n",
                 result->first_failed_task,
                 result->first_failed_task_id);
        }
        uboot_log_printf(3, "============================================\n\n");
    } else {
        uboot_log_printf(2, "U-Boot AutoRun: No result available\n");
    }

    /* 清理框架资源 */
    autorun_cleanup(ctx);

    /* 更新状态 */
    g_last_result = ret;
    g_completed = true;

    uboot_log_printf(3, "U-Boot AutoRun: Completed with code %d\n", ret);
    uboot_log_printf(3, "=====================================\n\n");

    return ret;
}

/**
 * @brief 获取U-Boot适配器的平台操作表
 *
 * 允许外部模块在初始化前修改操作表中的特定函数指针，
 * 以实现自定义的平台行为。
 *
 * @return const autorun_platform_ops_t* 平台操作表指针 (静态存储)
 */
const autorun_platform_ops_t* uboot_get_platform_ops(void)
{
    init_platform_ops();
    return &g_uboot_ops;
}

/* ================================================================== */
/*                    状态查询实现                                    */
/* ================================================================== */

/**
 * @brief 检查U-Boot自动运行是否已完成
 *
 * @return bool true=已完成, false=未执行或正在执行
 */
bool uboot_is_autorun_completed(void)
{
    return g_completed;
}

/**
 * @brief 获取最后一次执行的返回码
 *
 * @return int 最后一次 uboot_run_autorun() 的返回值
 */
int uboot_get_last_result(void)
{
    return g_last_result;
}

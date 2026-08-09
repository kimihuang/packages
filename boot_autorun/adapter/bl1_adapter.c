/**
 * @file bl1_adapter.c
 * @brief ATF BL1 阶段平台适配层实现
 *
 * 本文件实现了ARM Trusted Firmware (ATF) BL1阶段的平台适配，
 * 将ATF提供的底层能力映射到自动运行框架的标准接口。
 *
 * 实现要点:
 * 1. **日志适配**: ATF的 tf_log() → 框架的 log_printf()
 * 2. **命令执行**: 调用平台已实现的命令执行函数
 * 3. **时间管理**: ARM Generic Timer (CNTVCT_EL0)
 * 4. **内存操作**: 使用ATF的 libc 实现
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "bl1_adapter.h"
#include "../core/autorun_core.h"

/*
 * ATF 头文件包含
 */
#include <common/debug.h>           /* INFO, WARN, ERROR, NOTICE */
#include <drivers/console.h>        /* console驱动 */
#include <lib/libc/string.h>        /* memset, memcpy, strlen */
#include <lib/utils_def.h>          /* U(), LL宏 */
#include <arch_helpers.h>           /* read_cntvct_el0 */
#include <plat/common/platform.h>   /* 平台特定定义 */

/* ================================================================== */
/*                    内部常量定义                                    */
/* ================================================================== */

/** @brief 输出缓冲区大小 (栈上分配) */
#define OUTPUT_BUFFER_SIZE          256

/** @brief Generic Timer频率 (MHz) */
#ifndef PLAT_SYS_COUNTER_FREQ_IN_MHZ
#define PLAT_SYS_COUNTER_FREQ_IN_MHZ    24UL
#endif

/* ================================================================== */
/*                    全局状态变量                                    */
/* ================================================================== */

/** @brief 最后一次执行结果 */
static int g_last_result = 0;

/** @brief 执行完成标记 */
static bool g_completed = false;

/** @brief 平台操作函数表 (单例) */
static autorun_platform_ops_t g_bl1_ops;

/** @brief 操作表是否已初始化 */
static bool g_ops_initialized = false;

/* ================================================================== */
/*                 内部辅助函数声明                                   */
/* ================================================================== */

static void init_platform_ops(void);
static int map_log_level(int framework_level);
static uint64_t get_syscnt_freq(void);
static uint32_t ticks_to_ms(uint64_t ticks);
static uint64_t ms_to_ticks(uint32_t ms);

/* ================================================================== */
/*               内部辅助函数实现                                      */
/* ================================================================== */

static void init_platform_ops(void)
{
    if (g_ops_initialized) {
        return;
    }

    g_bl1_ops.log_printf = bl1_log_printf;
    g_bl1_ops.execute_command = bl1_execute_command;
    g_bl1_ops.get_timestamp = bl1_get_timestamp;
    g_bl1_ops.delay_ms = bl1_delay_ms;
    g_bl1_ops.memcpy = memcpy;
    g_bl1_ops.memset = memset;

    g_ops_initialized = true;
}

static int map_log_level(int framework_level)
{
    switch (framework_level) {
        case 0: return LOG_LEVEL_NONE;
        case 1: return LOG_LEVEL_ERROR;
        case 2: return LOG_LEVEL_WARNING;
        case 3: return LOG_LEVEL_INFO;
        case 4: return LOG_LEVEL_INFO;
        case 5: return LOG_LEVEL_VERBOSE;
        default: return LOG_LEVEL_INFO;
    }
}

static uint64_t get_syscnt_freq(void)
{
#ifdef plat_get_syscnt_freq2
    return plat_get_syscnt_freq2();
#elif defined(plat_get_syscnt_freq)
    return (uint64_t)plat_get_syscnt_freq();
#else
    return (uint64_t)PLAT_SYS_COUNTER_FREQ_IN_MHZ * 1000000ULL;
#endif
}

static uint32_t ticks_to_ms(uint64_t ticks)
{
    uint64_t freq = get_syscnt_freq();
    if (freq == 0) {
        return 0;
    }
    return (uint32_t)((ticks * 1000ULL) / freq);
}

static uint64_t ms_to_ticks(uint32_t ms)
{
    uint64_t freq = get_syscnt_freq();
    return ((uint64_t)ms * freq) / 1000ULL;
}

/* ================================================================== */
/*              平台操作函数实现 (供框架调用)                          */
/* ================================================================== */

void bl1_log_printf(int level, const char *fmt, ...)
{
    int atf_level = map_log_level(level);
#if LOG_LEVEL >= atf_level
    va_list args;
    va_start(args, fmt);
    tf_log(fmt, args);
    va_end(args);
#else
    (void)atf_level;
#endif
}

int bl1_execute_command(const char *command,
                        char *output,
                        size_t output_size,
                        uint32_t timeout_ms)
{
    /*
     * 命令执行函数。
     *
     * 在BL1环境中，此函数应调用平台已实现的命令执行接口。
     * 平台需要在集成时提供具体的命令执行实现。
     *
     * 如果平台未提供具体实现，此处返回错误。
     * 使用者可通过以下方式之一提供命令执行能力:
     * 1. 在平台代码中实现 plat_execute_command() 并在此调用
     * 2. 或在初始化前替换 g_bl1_ops.execute_command 函数指针
     */

    if (command == NULL || command[0] == '\0') {
        if (output != NULL && output_size > 0) {
            output[0] = '\0';
        }
        return -1;
    }

    if (output != NULL && output_size > 0) {
        snprintf(output, output_size,
                "Command execution not implemented in BL1 adapter: %.48s",
                command);
    }

    ERROR("BL1 Adapter: execute_command() not implemented\n");
    ERROR("  Platform must provide command execution via:\n");
    ERROR("  1. Implementing plat_execute_command() in platform code\n");
    ERROR("  2. Or replacing ops->execute_command before calling bl1_run_autorun()\n");

    return -1;
}

uint32_t bl1_get_timestamp(void)
{
    uint64_t current_ticks = read_cntvct_el0();
    return ticks_to_ms(current_ticks);
}

void bl1_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    uint64_t start_ticks = read_cntvct_el0();
    uint64_t delay_ticks = ms_to_ticks(ms);

    while ((read_cntvct_el0() - start_ticks) < delay_ticks) {
        __asm__ volatile("nop");
    }
}

/* ================================================================== */
/*                核心API函数实现                                     */
/* ================================================================== */

int bl1_run_autorun(void)
{
    return bl1_run_autorun_with_addr(
        PLAT_BL1_AUTORUN_CONFIG_ADDR,
        PLAT_BL1_AUTORUN_RESULT_ADDR,
        PLAT_BL1_AUTORUN_RESULT_SIZE
    );
}

int bl1_run_autorun_with_addr(uint32_t config_addr,
                              uint32_t result_addr,
                              uint32_t result_size)
{
    NOTICE("\n");
    NOTICE("=====================================\n");
    NOTICE("  BL1 AutoRun Framework Starting\n");
    NOTICE("=====================================\n");
    NOTICE("Config addr: 0x%08lX\n", (unsigned long)config_addr);
    NOTICE("Result addr: 0x%08lX\n", (unsigned long)result_addr);
    NOTICE("Result size: %u bytes\n", result_size);
    NOTICE("\n");

    init_platform_ops();

    autorun_context_t *ctx = NULL;
    int ret = autorun_init(config_addr,
                           result_addr,
                           result_size,
                           &g_bl1_ops,
                           &ctx);

    if (ret != 0) {
        ERROR("BL1 AutoRun: Init failed with error %d\n", ret);

        switch (ret) {
            case AUTORUN_ERR_INVALID_MAGIC:
                ERROR("  -> Invalid magic number\n");
                break;
            case AUTORUN_ERR_VERSION_MISMATCH:
                ERROR("  -> Version mismatch\n");
                break;
            case AUTORUN_ERR_CRC_ERROR:
                ERROR("  -> CRC32 check failed\n");
                break;
            case AUTORUN_ERR_INVALID_PARAM:
                ERROR("  -> Invalid parameter\n");
                break;
            case AUTORUN_ERR_NO_MEMORY:
                ERROR("  -> Result buffer too small\n");
                break;
            default:
                ERROR("  -> Unknown error code\n");
                break;
        }

        g_last_result = ret;
        g_completed = true;
        return ret;
    }

    INFO("BL1 AutoRun: Framework initialized successfully\n");

    autorun_options_t options;
    memset(&options, 0, sizeof(options));

    options.stop_on_critical_fail = true;
    options.capture_full_output = false;
    options.global_timeout_ms = 60000;
    options.verbose_logging = false;

    autorun_set_options(ctx, &options);

    INFO("BL1 AutoRun: Executing stage 'bl1'...\n");
    ret = autorun_run_stage(ctx, "bl1");

    const autorun_result_t *result = autorun_get_result(ctx);
    if (result != NULL) {
        NOTICE("\n");
        NOTICE("========== BL1 AutoRun Result ==========\n");
        NOTICE("Total tasks:  %u\n", result->total_tasks);
        NOTICE("Passed:       %u\n", result->passed_count);
        NOTICE("Failed:       %u\n", result->failed_count);
        NOTICE("Skipped:      %u\n", result->skipped_count);
        NOTICE("Elapsed time: %u ms\n", result->total_elapsed_ms);
        NOTICE("Overall:      %s\n",
               (result->overall_status == 0) ? "PASS" : "FAIL");

        if (result->failed_count > 0 &&
            result->first_failed_task[0] != '\0') {
            WARN("First failure: %s (ID=%u)\n",
                 result->first_failed_task,
                 result->first_failed_task_id);
        }
        NOTICE("==========================================\n\n");
    } else {
        WARN("BL1 AutoRun: No result available\n");
    }

    autorun_cleanup(ctx);

    g_last_result = ret;
    g_completed = true;

    NOTICE("BL1 AutoRun: Completed with code %d\n", ret);
    NOTICE("=====================================\n\n");

    return ret;
}

const autorun_platform_ops_t* bl1_get_platform_ops(void)
{
    init_platform_ops();
    return &g_bl1_ops;
}

/* ================================================================== */
/*                    状态查询实现                                    */
/* ================================================================== */

bool bl1_is_autorun_completed(void)
{
    return g_completed;
}

int bl1_get_last_result(void)
{
    return g_last_result;
}

/**
 * @file bl31_adapter.c
 * @brief ATF BL31 阶段平台适配层实现
 *
 * 本文件实现了ARM Trusted Firmware (ATF) BL31阶段的平台适配，
 * 将ATF EL3运行环境提供的能力映射到自动运行框架的标准接口。
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "bl31_adapter.h"
#include "../core/autorun_core.h"

#include <common/debug.h>
#include <drivers/console.h>
#include <lib/libc/string.h>
#include <lib/utils_def.h>
#include <arch_helpers.h>
#include <plat/common/platform.h>

/* ================================================================== */
/*                    内部常量定义                                    */
/* ================================================================== */

#define OUTPUT_BUFFER_SIZE          512

#ifndef PLAT_SYS_COUNTER_FREQ_IN_MHZ
#define PLAT_SYS_COUNTER_FREQ_IN_MHZ    24UL
#endif

/* ================================================================== */
/*                    全局状态变量                                    */
/* ================================================================== */

static int g_last_result = 0;
static bool g_completed = false;
static autorun_platform_ops_t g_bl31_ops;
static bool g_ops_initialized = false;

/* ================================================================== */
/*                 内部辅助函数                                       */
/* ================================================================== */

static void init_platform_ops(void)
{
    if (g_ops_initialized) {
        return;
    }

    g_bl31_ops.log_printf = bl31_log_printf;
    g_bl31_ops.execute_command = bl31_execute_command;
    g_bl31_ops.get_timestamp = bl31_get_timestamp;
    g_bl31_ops.delay_ms = bl31_delay_ms;
    g_bl31_ops.memcpy = memcpy;
    g_bl31_ops.memset = memset;

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
/*              平台操作函数实现                                      */
/* ================================================================== */

void bl31_log_printf(int level, const char *fmt, ...)
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

int bl31_execute_command(const char *command,
                         char *output,
                         size_t output_size,
                         uint32_t timeout_ms)
{
    /*
     * 命令执行函数。
     *
     * 在BL31环境中，此函数应调用平台已实现的命令执行接口。
     * 平台需要在集成时提供具体的命令执行实现。
     *
     * 如果平台未提供具体实现，此处返回错误。
     * 使用者可通过以下方式之一提供命令执行能力:
     * 1. 在平台代码中实现 plat_execute_command() 并在此调用
     * 2. 或在初始化前替换 g_bl31_ops.execute_command 函数指针
     */

    if (command == NULL || command[0] == '\0') {
        if (output != NULL && output_size > 0) {
            output[0] = '\0';
        }
        return -1;
    }

    if (output != NULL && output_size > 0) {
        snprintf(output, output_size,
                "Command execution not implemented in BL31 adapter: %.48s",
                command);
    }

    ERROR("BL31 Adapter: execute_command() not implemented\n");
    ERROR("  Platform must provide command execution via:\n");
    ERROR("  1. Implementing plat_execute_command() in platform code\n");
    ERROR("  2. Or replacing ops->execute_command before calling bl31_run_autorun()\n");

    return -1;
}

uint32_t bl31_get_timestamp(void)
{
    uint64_t current_ticks = read_cntpct_el0();
    return ticks_to_ms(current_ticks);
}

void bl31_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    uint64_t start_ticks = read_cntpct_el0();
    uint64_t delay_ticks = ms_to_ticks(ms);

    while ((read_cntpct_el0() - start_ticks) < delay_ticks) {
        __asm__ volatile("nop");
    }
}

/* ================================================================== */
/*                核心API函数实现                                     */
/* ================================================================== */

int bl31_run_autorun(void)
{
    return bl31_run_autorun_with_addr(
        PLAT_BL31_AUTORUN_CONFIG_ADDR,
        PLAT_BL31_AUTORUN_RESULT_ADDR,
        PLAT_BL31_AUTORUN_RESULT_SIZE
    );
}

int bl31_run_autorun_with_addr(uint32_t config_addr,
                               uint32_t result_addr,
                               uint32_t result_size)
{
    NOTICE("\n");
    NOTICE("======================================\n");
    NOTICE("  BL31 AutoRun Framework Starting\n");
    NOTICE("======================================\n");
    NOTICE("Config addr:  0x%08lX\n", (unsigned long)config_addr);
    NOTICE("Result addr: 0x%08lX\n", (unsigned long)result_addr);
    NOTICE("Result size: %u bytes\n", result_size);
    NOTICE("\n");

    init_platform_ops();

    autorun_context_t *ctx = NULL;
    int ret = autorun_init(config_addr,
                           result_addr,
                           result_size,
                           &g_bl31_ops,
                           &ctx);

    if (ret != 0) {
        ERROR("BL31 AutoRun: Init failed with error %d\n", ret);

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

    INFO("BL31 AutoRun: Framework initialized successfully\n");

    autorun_options_t options;
    memset(&options, 0, sizeof(options));

    options.stop_on_critical_fail = true;
    options.capture_full_output = true;
    options.global_timeout_ms = 120000;
    options.verbose_logging = true;

    autorun_set_options(ctx, &options);

    INFO("BL31 AutoRun: Executing stage 'bl31'...\n");
    ret = autorun_run_stage(ctx, "bl31");

    const autorun_result_t *result = autorun_get_result(ctx);
    if (result != NULL) {
        NOTICE("\n");
        NOTICE("========== BL31 AutoRun Result ==========\n");
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
        NOTICE("===========================================\n\n");
    } else {
        WARN("BL31 AutoRun: No result available\n");
    }

    autorun_cleanup(ctx);

    g_last_result = ret;
    g_completed = true;

    NOTICE("BL31 AutoRun: Completed with code %d\n", ret);
    NOTICE("======================================\n\n");

    return ret;
}

const autorun_platform_ops_t* bl31_get_platform_ops(void)
{
    init_platform_ops();
    return &g_bl31_ops;
}

/* ================================================================== */
/*                    状态查询实现                                    */
/* ================================================================== */

bool bl31_is_autorun_completed(void)
{
    return g_completed;
}

int bl31_get_last_result(void)
{
    return g_last_result;
}

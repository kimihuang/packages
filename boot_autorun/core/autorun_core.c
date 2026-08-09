/**
 * @file autorun_core.c
 * @brief 启动各阶段自动运行框架 - 核心引擎实现
 *
 * 本文件实现了自动运行框架的核心调度逻辑，包括：
 * - 框架初始化与配置加载
 * - 阶段任务的调度与执行
 * - 执行结果的收集与管理
 * - 框架生命周期控制
 *
 * 核心设计:
 * 1. 上下文驱动: 所有操作基于 autorun_context_t 上下文
 * 2. 平台解耦: 通过 ops 接口适配不同运行环境
 * 3. 事件驱动: 通过回调函数支持外部监控和干预
 * 4. 状态机: 明确的状态转换保证操作安全性
 *
 * 调用关系:
 *   用户代码 / 平台适配层
 *       └──→ autorun_core API (本模块)
 *               ├──→ task_executor_execute() (任务执行)
 *               ├──→ autorun_validate_config() (配置校验)
 *               └──→ 写入 autorun_result_t (结果记录)
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "autorun_core.h"
#include "task_executor.h"
#include "../utils/autorun_log.h"

/* ================================================================== */
/*                         内部宏定义                                  */
/* ================================================================== */

/**
 * @brief 默认全局超时时间 (毫秒)
 *
 * 当 options.global_timeout_ms = 0 且 config 中也未指定时使用。
 * 设置为30分钟，适用于大多数测试场景。
 */
#define DEFAULT_GLOBAL_TIMEOUT_MS    (30 * 60 * 1000)

/**
 * @brief 结果区域最小安全大小 (bytes)
 *
 * 至少能容纳结果头部 + 一个任务的结果。
 * 用于防止缓冲区过小导致溢出。
 */
#define MIN_RESULT_BUFFER_SIZE       (sizeof(autorun_result_t) + \
                                     sizeof(task_result_t))

/* ================================================================== */
/*                    内部辅助函数声明                                 */
/* ================================================================== */

/**
 * @brief 校验配置数据的完整性和有效性
 */
static int validate_config_internal(const autorun_config_t *config);

/**
 * @brief 初始化结果区域的头部信息
 */
static int init_result_header(autorun_context_t *ctx);

/**
 * @brief 计算总任务数并初始化结果数组空间
 */
static int init_result_array(autorun_context_t *ctx);

/**
 * @brief 执行阶段的前置命令列表
 */
static int execute_pre_commands(autorun_context_t *ctx,
                                const stage_config_t *stage);

/**
 * @brief 执行阶段的后置命令列表
 */
static int execute_post_commands(autorun_context_t *ctx,
                                 const stage_config_t *stage);

/**
 * @brief 执行单个阶段的所有任务
 */
static int execute_stage_tasks(autorun_context_t *ctx,
                               const stage_config_t *stage);

/**
 * @brief 更新框架的统计信息
 */
static void update_statistics(autorun_context_t *ctx,
                              const executor_output_t *task_output,
                              const task_config_t *task);

/**
 * @brief 将单个任务结果写入结果区域
 */
static int write_task_result(autorun_context_t *ctx,
                             uint32_t result_index,
                             const executor_output_t *executor_output,
                             const task_config_t *task);

/**
 * @brief 更新结果区域的汇总统计
 */
static void update_result_summary(autorun_context_t *ctx);

/**
 * @brief 检查是否超过全局超时
 */
static bool check_global_timeout(autorun_context_t *ctx);

/**
 * @brief 安全的日志输出包装
 */
static void ctx_log(autorun_context_t *ctx,
                    int level,
                    const char *fmt, ...);

/* ================================================================== */
/*                 公开API函数实现 - 初始化                             */
/* ================================================================== */

int autorun_init(uint32_t config_addr,
                uint32_t result_addr,
                uint32_t result_size,
                const autorun_platform_ops_t *ops,
                autorun_context_t **ctx_out)
{
    /*
     * 物理地址版本初始化。
     *
     * 在大多数嵌入式环境中，配置数据通过JTAG/UART烧录到固定物理地址，
     * 此函数直接使用物理地址访问。在某些有MMU的环境中，
     * 可能需要先进行地址映射（由调用者或平台适配层处理）。
     */

    /* 参数校验 */
    if (ops == NULL || ctx_out == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (config_addr == 0 || result_addr == 0) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (result_size < MIN_RESULT_BUFFER_SIZE) {
        return AUTORUN_ERR_NO_MEMORY;
    }

    /* 将物理地址转换为指针 (假设平台支持直接访问) */
    const autorun_config_t *config =
        (const autorun_config_t *)(uintptr_t)config_addr;
    autorun_result_t *result =
        (autorun_result_t *)(uintptr_t)result_addr;

    /* 委托给指针版本完成实际初始化 */
    return autorun_init_mapped(config, result, result_size, ops, ctx_out);
}

int autorun_init_mapped(const autorun_config_t *config,
                       autorun_result_t *result,
                       uint32_t result_size,
                       const autorun_platform_ops_t *ops,
                       autorun_context_t **ctx_out)
{
    /*
     * 指针版本初始化。
     *
     * 适用于已经完成内存映射的场景：
     * - Linux用户态程序 (通过mmap映射/dev/mem)
     * - U-Boot/RT-Thread (DDR可直接访问)
     * - 单元测试 (栈/堆上的模拟数据)
     */

    /* ===== 参数校验 ===== */
    if (config == NULL || result == NULL || ops == NULL || ctx_out == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* 校验平台操作表的关键函数 */
    if (ops->log_printf == NULL ||
        ops->memset == NULL ||
        ops->memcpy == NULL ||
        ops->get_timestamp == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* ===== 配置数据校验 ===== */
    int ret = validate_config_internal(config);
    if (ret != 0) {
        return ret;  /* 返回具体的校验错误码 */
    }

    /* ===== 分配上下文结构体 ===== */
    /*
     * 注意: 在资源受限环境(BL1)中，可能需要静态分配或从特定内存池获取。
     * 这里使用标准malloc，平台可根据需要替换为自定义分配器。
     */
    autorun_context_t *ctx =
        (autorun_context_t *)ops->memset(NULL, 0, sizeof(autorun_context_t));

    if (ctx == NULL) {
        return AUTORUN_ERR_NO_MEMORY;
    }

    /* 使用memset清零上下文 (通过ops接口) */
    ops->memset(ctx, 0, sizeof(autorun_context_t));

    /* ===== 填充上下文字段 ===== */

    /* 配置信息 */
    ctx->config = config;
    ctx->config_validated = true;

    /* 运行状态 */
    ctx->state = AUTORUN_STATE_READY;
    ctx->current_stage_index = -1;
    ctx->current_task_index = -1;

    /* 平台接口 */
    ctx->ops = ops;

    /* 结果存储 */
    ctx->result = result;
    ctx->result_size = result_size;

    /* 初始化结果区域 */
    ret = init_result_header(ctx);
    if (ret != 0) {
        /* 结果区域初始化失败，释放上下文 */
        /* 注意: 如果使用了自定义分配器，这里应该调用对应的释放函数 */
        return ret;
    }

    /* 初始化结果数组空间 */
    ret = init_result_array(ctx);
    if (ret != 0) {
        return ret;
    }

    /* 设置默认选项 */
    ctx->options.stop_on_critical_fail = true;
    ctx->options.capture_full_output = false;
    ctx->options.global_timeout_ms = DEFAULT_GLOBAL_TIMEOUT_MS;
    ctx->options.verbose_logging = false;

    /* 从配置继承全局设置 */
    if (config->global_config.global_timeout_s > 0) {
        ctx->options.global_timeout_ms =
            (uint32_t)config->global_config.global_timeout_s * 1000;
    }

    /* 记录启动时间戳 */
    ctx->start_timestamp = ops->get_timestamp();

    /* 输出初始化成功日志 */
    ctx_log(ctx, LOG_LEVEL_INFO,
           "Framework initialized successfully");
    ctx_log(ctx, LOG_LEVEL_DEBUG,
           "Config addr=%p, Result addr=%p, Size=%u bytes",
           (void *)config, (void *)result, result_size);

    /* 返回上下文 */
    *ctx_out = ctx;

    return AUTORUN_OK;
}

/* ================================================================== */
/*                 公开API函数实现 - 配置                               */
/* ================================================================== */

int autorun_set_options(autorun_context_t *ctx,
                        const autorun_options_t *opts)
{
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (ctx->state != AUTORUN_STATE_READY &&
        ctx->state != AUTORUN_STATE_COMPLETED) {
        /* 只能在READY或COMPLETED状态下修改选项 */
        ctx_log(ctx, LOG_LEVEL_WARN,
               "Cannot set options in state %s",
               autorun_state_string(ctx->state));
        return AUTORUN_ERR_INTERNAL;
    }

    if (opts != NULL) {
        /* 复制用户提供的选项 */
        ctx->ops->memcpy(&ctx->options, opts, sizeof(autorun_options_t));
    } else {
        /* 重置为默认值 */
        ctx->options.stop_on_critical_fail = true;
        ctx->options.capture_full_output = false;
        ctx->options.global_timeout_ms = DEFAULT_GLOBAL_TIMEOUT_MS;
        ctx->options.verbose_logging = false;
    }

    ctx_log(ctx, LOG_LEVEL_DEBUG,
           "Options updated: stop_on_crit_fail=%d, timeout=%ums",
           ctx->options.stop_on_critical_fail,
           ctx->options.global_timeout_ms);

    return AUTORUN_OK;
}

int autorun_set_callbacks(autorun_context_t *ctx,
                          autorun_task_start_cb_t on_task_start,
                          autorun_task_complete_cb_t on_task_complete,
                          autorun_stage_start_cb_t on_stage_start,
                          autorun_stage_complete_cb_t on_stage_complete)
{
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* 注册回调函数 (允许NULL表示不启用) */
    ctx->on_task_start = on_task_start;
    ctx->on_task_complete = on_task_complete;
    ctx->on_stage_start = on_stage_start;
    ctx->on_stage_complete = on_stage_complete;

    ctx_log(ctx, LOG_LEVEL_DEBUG,
           "Callbacks registered: task_start=%p, task_complete=%p, "
           "stage_start=%p, stage_complete=%p",
           (void *)on_task_start, (void *)on_task_complete,
           (void *)on_stage_start, (void *)on_stage_complete);

    return AUTORUN_OK;
}

/* ================================================================== */
/*                 公开API函数实现 - 执行                               */
/* ================================================================== */

int autorun_run_stage(autorun_context_t *ctx,
                     const char *stage_name)
{
    /*
     * 执行指定阶段的所有任务。
     *
     * 这是框架的核心执行函数，完成以下工作:
     * 1. 查找阶段配置
     * 2. 检查阶段是否启用
     * 3. 触发阶段开始回调
     * 4. 执行前置命令 (pre_commands)
     * 5. 遍历执行所有任务
     * 6. 执行后置命令 (post_commands)
     * 7. 触发阶段完成回调
     * 8. 更新统计信息和结果
     */

    /* ===== 参数校验 ===== */
    if (ctx == NULL || stage_name == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* ===== 状态检查 ===== */
    if (ctx->state != AUTORUN_STATE_READY &&
        ctx->state != AUTORUN_STATE_COMPLETED) {
        /* 只能在READY或COMPLETED状态下执行新阶段 */
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Cannot run stage in state %s",
               autorun_state_string(ctx->state));
        return AUTORUN_ERR_INTERNAL;
    }

    /* ===== 查找阶段配置 ===== */
    const stage_config_t *stage = autorun_find_stage(ctx->config, stage_name);

    if (stage == NULL) {
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Stage '%s' not found in configuration", stage_name);
        return AUTORUN_ERR_STAGE_NOT_FOUND;
    }

    /* ===== 检查阶段是否启用 ===== */
    if (!stage->enabled) {
        ctx_log(ctx, LOG_LEVEL_WARN,
               "Stage '%s' is disabled, skipping", stage_name);
        return AUTORUN_OK;  /* 未启用的阶段不算错误 */
    }

    /* ===== 检查全局超时 ===== */
    if (check_global_timeout(ctx)) {
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Global timeout exceeded before stage '%s'", stage_name);
        ctx->state = AUTORUN_STATE_ERROR;
        return AUTORUN_ERR_TIMEOUT;
    }

    /* ===== 更新状态为 RUNNING ===== */
    ctx->state = AUTORUN_STATE_RUNNING;

    /* 记录当前阶段索引 */
    for (uint32_t i = 0; i < ctx->config->stage_count; i++) {
        if (&ctx->config->stages[i] == stage) {
            ctx->current_stage_index = (int)i;
            break;
        }
    }

    /* 记录阶段开始时间 */
    ctx->stage_start_timestamp = ctx->ops->get_timestamp();

    ctx_log(ctx, LOG_LEVEL_INFO,
           "========== Stage [%s] started ==========", stage_name);

    /* ===== 触发阶段开始回调 ===== */
    if (ctx->on_stage_start != NULL) {
        int cb_ret = ctx->on_stage_start(ctx, stage);
        if (cb_ret != 0) {
            ctx_log(ctx, LOG_LEVEL_WARN,
                   "Stage start callback returned %d, skipping stage", cb_ret);
            ctx->state = AUTORUN_STATE_READY;
            return AUTORUN_OK;  /* 回调决定跳过 */
        }
    }

    /* ===== 执行前置命令 ===== */
    if (stage->pre_command_count > 0) {
        int ret = execute_pre_commands(ctx, stage);
        if (ret != 0 && !ctx->config->global_config.continue_on_error) {
            ctx_log(ctx, LOG_LEVEL_ERROR,
                   "Pre-commands failed for stage '%s', aborting", stage_name);
            ctx->state = AUTORUN_STATE_ERROR;
            update_result_summary(ctx);
            return ret;
        }
    }

    /* ===== 执行阶段的所有任务 ===== */
    int task_ret = execute_stage_tasks(ctx, stage);

    /* ===== 执行后置命令 ===== */
    if (stage->post_command_count > 0) {
        int post_ret = execute_post_commands(ctx, stage);
        if (post_ret != 0 && task_ret == 0) {
            task_ret = post_ret;  /* 后置命令失败也视为阶段失败 */
        }
    }

    /* ===== 计算阶段耗时 ===== */
    uint32_t stage_end_time = ctx->ops->get_timestamp();
    uint32_t stage_elapsed = stage_end_time - ctx->stage_start_timestamp;

    /* ===== 更新结果汇总 ===== */
    ctx->result->completed_stages++;
    ctx->ops->memcpy(ctx->result->last_stage, stage_name,
                    strlen(stage_name) + 1);

    update_result_summary(ctx);

    /* ===== 触发阶段完成回调 ===== */
    if (ctx->on_stage_complete != NULL) {
        /* 构造临时的阶段结果用于回调 */
        autorun_result_t stage_result_copy;
        ctx->ops->memcpy(&stage_result_copy, ctx->result,
                        sizeof(autorun_result_t));
        /* 注意: 不拷贝变长数组部分 */

        int cb_ret = ctx->on_stage_complete(ctx, stage, &stage_result_copy);
        if (cb_ret != 0) {
            ctx_log(ctx, LOG_LEVEL_WARN,
                   "Stage complete callback requested abort (ret=%d)", cb_ret);
            task_ret = (task_ret == 0) ? AUTORUN_ERR_CRITICAL_FAIL : task_ret;
        }
    }

    /* ===== 日志汇总 ===== */
    ctx_log(ctx, LOG_LEVEL_INFO,
           "========== Stage [%s] completed (%ums) ==========",
           stage_name, stage_elapsed);

    ctx_log(ctx, LOG_LEVEL_INFO,
           "Stage summary: total=%u, passed=%u, failed=%u, skipped=%u",
           ctx->statistics.completed_tasks,
           ctx->statistics.passed_tasks,
           ctx->statistics.failed_tasks,
           ctx->statistics.skipped_tasks);

    /* ===== 更新状态 ===== */
    if (task_ret != 0) {
        ctx->state = AUTORUN_STATE_ERROR;
    } else {
        ctx->state = AUTORUN_STATE_COMPLETED;
    }

    /* 重置当前任务索引 */
    ctx->current_task_index = -1;

    return task_ret;
}

int autorun_run_all(autorun_context_t *ctx)
{
    /*
     * 按照execution_order顺序执行所有启用的阶段。
     *
     * 适用场景:
     * - 需要一次性跑完全部流程
     * - 自动化测试脚本
     * - 生产环境批量验证
     */

    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    ctx_log(ctx, LOG_LEVEL_INFO,
           "Starting execution of all stages...");

    /* 记录总体开始时间 */
    ctx->start_timestamp = ctx->ops->get_timestamp();
    ctx->result->timestamp_start = ctx->start_timestamp;

    int overall_ret = AUTORUN_OK;
    bool aborted = false;

    /* 遍历所有阶段，按execution_order排序 */
    for (uint32_t idx = 0; idx < ctx->config->stage_count && !aborted; idx++) {

        /* 在每轮中查找具有最小execution_order的未执行阶段 */
        int best_idx = -1;
        uint8_t best_order = 255;

        for (uint32_t i = 0; i < ctx->config->stage_count; i++) {
            const stage_config_t *stage = &ctx->config->stages[i];

            /* 跳过已完成的阶段 (简单标记: stage_name以\0开头表示已完成) */
            if (stage->stage_name[0] == '\0') {
                continue;
            }

            /* 跳过未启用的阶段 */
            if (!stage->enabled) {
                continue;
            }

            /* 寻找最小的execution_order */
            if (stage->execution_order < best_order) {
                best_order = stage->execution_order;
                best_idx = (int)i;
            }
        }

        /* 没有找到可执行的阶段 */
        if (best_idx < 0) {
            break;
        }

        /* 找到了下一个要执行的阶段 */
        const stage_config_t *next_stage = &ctx->config->stages[best_idx];
        int ret = autorun_run_stage(ctx, next_stage->stage_name);

        if (ret != 0) {
            overall_ret = ret;

            /* 检查是否应该中止后续阶段 */
            if (ctx->options.stop_on_critical_fail ||
                !ctx->config->global_config.continue_on_error) {
                ctx_log(ctx, LOG_LEVEL_ERROR,
                       "Aborting remaining stages due to failure in '%s'",
                       next_stage->stage_name);
                aborted = true;
            }
        }

        /* 标记此阶段为已完成 (临时方案: 清空名称) */
        /* 注意: 这会修改原始配置数据，更好的方案是使用独立的跟踪数组 */
    }

    /* 记录总体结束时间 */
    ctx->result->timestamp_end = ctx->ops->get_timestamp();
    ctx->result->total_elapsed_ms =
        ctx->result->timestamp_end - ctx->result->timestamp_start;

    /* 更新总统计 */
    ctx->statistics.total_elapsed_ms = ctx->result->total_elapsed_ms;

    /* 最终日志 */
    ctx_log(ctx, LOG_LEVEL_INFO,
           "========================================");
    ctx_log(ctx, LOG_LEVEL_INFO,
           "All stages execution completed");
    ctx_log(ctx, LOG_LEVEL_INFO,
           "Overall: %s (ret=%d)",
           (overall_ret == AUTORUN_OK) ? "PASS" : "FAIL", overall_ret);
    ctx_log(ctx, LOG_LEVEL_INFO,
           "Total elapsed: %ums", ctx->result->total_elapsed_ms);
    ctx_log(ctx, LOG_LEVEL_INFO,
           "========================================");

    /* 根据配置决定是否停机 */
    if (ctx->config->global_config.halt_on_completion) {
        ctx_log(ctx, LOG_LEVEL_INFO,
               "Halt on completion enabled, entering infinite loop...");
        while (1) {
            /* 无限循环，等待PC侧读取结果或手动复位 */
            ctx->ops->delay_ms(1000);
        }
    }

    return overall_ret;
}

int autorun_pause(autorun_context_t *ctx)
{
    /* 预留功能: 暂停执行 */
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (ctx->state != AUTORUN_STATE_RUNNING) {
        return AUTORUN_ERR_INTERNAL;
    }

    ctx->state = AUTORUN_STATE_PAUSED;
    ctx_log(ctx, LOG_LEVEL_INFO, "Execution paused");

    return AUTORUN_OK;
}

int autorun_resume(autorun_context_t *ctx)
{
    /* 预留功能: 恢复执行 */
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (ctx->state != AUTORUN_STATE_PAUSED) {
        return AUTORUN_ERR_INTERNAL;
    }

    ctx->state = AUTORUN_STATE_RUNNING;
    ctx_log(ctx, LOG_LEVEL_INFO, "Execution resumed");

    return AUTORUN_OK;
}

int autorun_abort(autorun_context_t *ctx)
{
    /* 中止执行 */
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (ctx->state != AUTORUN_STATE_RUNNING &&
        ctx->state != AUTORUN_STATE_PAUSED) {
        return AUTORUN_ERR_INTERNAL;
    }

    ctx_log(ctx, LOG_LEVEL_WARN, "Execution aborted by user request");

    /* 将当前任务标记为SKIPPED (如果正在执行) */
    if (ctx->current_task_index >= 0) {
        /* TODO: 标记当前任务状态 */
    }

    ctx->state = AUTORUN_STATE_ERROR;
    update_result_summary(ctx);

    return AUTORUN_OK;
}

/* ================================================================== */
/*                 公开API函数实现 - 状态查询                           */
/* ================================================================== */

autorun_state_t autorun_get_state(const autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return AUTORUN_STATE_UNINITIALIZED;
    }
    return ctx->state;
}

const autorun_result_t* autorun_get_result(const autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->result;
}

int autorun_get_statistics(const autorun_context_t *ctx,
                           autorun_statistics_t *stats)
{
    if (ctx == NULL || stats == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    ctx->ops->memcpy(stats, &ctx->statistics, sizeof(autorun_statistics_t));
    return AUTORUN_OK;
}

const task_result_t* autorun_get_task_result(const autorun_context_t *ctx,
                                            uint32_t task_id)
{
    if (ctx == NULL || ctx->result == NULL) {
        return NULL;
    }

    /* 在结果数组中查找匹配的task_id */
    uint32_t total = ctx->result->total_tasks;
    for (uint32_t i = 0; i < total; i++) {
        if (ctx->result->results[i].task_id == task_id) {
            return &ctx->result->results[i];
        }
    }

    return NULL;  /* 未找到 */
}

bool autorun_has_failures(const autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    return (ctx->statistics.failed_tasks > 0);
}

bool autorun_has_critical_failure(const autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    /* 关键任务失败会设置 last_error_code 为 CRITICAL_FAIL */
    return (ctx->statistics.last_error_code == AUTORUN_ERR_CRITICAL_FAIL);
}

/* ================================================================== */
/*                 公开API函数实现 - 工具函数                           */
/* ================================================================== */

int autorun_validate_config(const autorun_config_t *config)
{
    return validate_config_internal(config);
}

uint32_t autorun_get_config_size(const autorun_config_t *config)
{
    if (config == NULL) {
        return 0;
    }

    /*
     * 配置结构的实际大小计算:
     * 固定部分 + 变长的stages数组
     *
     * 由于每个阶段的tasks[]是固定大小的数组(AUTORUN_MAX_TASKS_PER_STAGE),
     * 所以整个结构的大小是固定的，等于 header.config_size 字段的值。
     */

    /* 优先使用配置中记录的大小 */
    if (config->header.magic == AUTORUN_MAGIC &&
        config->header.config_size > 0) {
        return config->header.config_size;
    }

    /* 否则计算理论大小 */
    return sizeof(autorun_config_t);
}

uint32_t autorun_get_result_size(const autorun_result_t *result)
{
    if (result == NULL) {
        return 0;
    }

    /*
     * 结果结构包含变长数组 results[]:
     * 固定部分大小 + results元素个数 * 每个元素大小
     */
    return sizeof(autorun_result_t) +
           result->total_tasks * sizeof(task_result_t);
}

const stage_config_t* autorun_find_stage(
    const autorun_config_t *config,
    const char *stage_name)
{
    if (config == NULL || stage_name == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < config->stage_count; i++) {
        if (strncmp(config->stages[i].stage_name, stage_name,
                    AUTORUN_STAGE_NAME_MAX_LEN) == 0) {
            return &config->stages[i];
        }
    }

    return NULL;  /* 未找到 */
}

const task_config_t* autorun_find_task(
    const autorun_config_t *config,
    uint32_t task_id)
{
    if (config == NULL) {
        return NULL;
    }

    /* 遍历所有阶段的所有任务 */
    for (uint32_t s = 0; s < config->stage_count; s++) {
        const stage_config_t *stage = &config->stages[s];
        for (uint8_t t = 0; t < stage->task_count; t++) {
            if (stage->tasks[t].id == task_id) {
                return &stage->tasks[t];
            }
        }
    }

    return NULL;  /* 未找到 */
}

const char* autorun_state_string(autorun_state_t state)
{
    switch (state) {
        case AUTORUN_STATE_UNINITIALIZED: return "UNINITIALIZED";
        case AUTORUN_STATE_READY:          return "READY";
        case AUTORUN_STATE_RUNNING:        return "RUNNING";
        case AUTORUN_STATE_PAUSED:         return "PAUSED";
        case AUTORUN_STATE_COMPLETED:      return "COMPLETED";
        case AUTORUN_STATE_ERROR:          return "ERROR";
        case AUTORUN_STATE_CLEANED:        return "CLEANED";
        default:                          return "UNKNOWN";
    }
}

const char* autorun_task_status_string(task_status_t status)
{
    switch (status) {
        case TASK_STATUS_PENDING:  return "PENDING";
        case TASK_STATUS_RUNNING:  return "RUNNING";
        case TASK_STATUS_SUCCESS:  return "SUCCESS";
        case TASK_STATUS_FAILED:   return "FAILED";
        case TASK_STATUS_TIMEOUT:  return "TIMEOUT";
        case TASK_STATUS_SKIPPED:  return "SKIPPED";
        case TASK_STATUS_ERROR:    return "ERROR";
        default:                  return "UNKNOWN";
    }
}

/* ================================================================== */
/*                 公开API函数实现 - 清理与销毁                         */
/* ================================================================== */

int autorun_cleanup(autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    ctx_log(ctx, LOG_LEVEL_DEBUG, "Cleaning up framework resources");

    /* 清空敏感信息 */
    ctx->config = NULL;
    ctx->result = NULL;
    ctx->ops = NULL;

    /* 重置状态 */
    ctx->state = AUTORUN_STATE_CLEANED;

    /* 注意: 不释放ctx本身的内存，因为可能是静态分配的 */
    /* 如果是动态分配的，调用者应负责释放 */

    return AUTORUN_OK;
}

int autorun_reset(autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* 只能在非运行状态下重置 */
    if (ctx->state == AUTORUN_STATE_RUNNING ||
        ctx->state == AUTORUN_STATE_PAUSED) {
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Cannot reset while running or paused");
        return AUTORUN_ERR_INTERNAL;
    }

    ctx_log(ctx, LOG_LEVEL_DEBUG, "Resetting framework state");

    /* 保留配置和平台接口，重置其他状态 */
    ctx->state = AUTORUN_STATE_READY;
    ctx->current_stage_index = -1;
    ctx->current_task_index = -1;

    /* 重置统计信息 */
    ctx->ops->memset(&ctx->statistics, 0, sizeof(autorun_statistics_t));

    /* 重新初始化结果区域 */
    init_result_header(ctx);
    init_result_array(ctx);

    /* 重置时间戳 */
    ctx->start_timestamp = ctx->ops->get_timestamp();
    ctx->stage_start_timestamp = 0;

    return AUTORUN_OK;
}

/* ================================================================== */
/*               内部辅助函数实现                                      */
/* ================================================================== */

static int validate_config_internal(const autorun_config_t *config)
{
    /* ===== 1. 指针有效性检查 ===== */
    if (config == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* ===== 2. 魔数检查 ===== */
    if (config->header.magic != AUTORUN_MAGIC) {
        return AUTORUN_ERR_INVALID_MAGIC;
    }

    /* ===== 3. 版本兼容性检查 ===== */
    /*
     * 版本兼容性规则:
     * - 主版本(Major)必须严格匹配
     * - 次版本(Minor): 框架版本 >= 配置版本 (可读取旧版配置)
     */
    if (config->header.version_major != AUTORUN_VERSION_MAJOR) {
        return AUTORUN_ERR_VERSION_MISMATCH;
    }

    /* ===== 4. 大小合理性检查 ===== */
    if (config->header.config_size < sizeof(autorun_config_t)) {
        return AUTORUN_ERR_SIZE_INVALID;
    }

    /* ===== 5. CRC32校验 ===== */
    /*
     * CRC32计算范围: 从结构体起始到crc32字段之前
     * 即: 整个结构减去最后4字节(crc32字段本身)
     */
    uint32_t crc_data_len = config->header.config_size -
                            offsetof(autorun_config_t, header) -
                            offsetof(autorun_config_header_t, crc32);

    uint32_t calculated_crc = autorun_calc_crc32(
        (const uint8_t *)config,
        config->header.config_size - sizeof(uint32_t)
    );

    if (calculated_crc != config->header.crc32) {
        return AUTORUN_ERR_CRC_ERROR;
    }

    /* ===== 6. 阶段数量检查 ===== */
    if (config->stage_count > AUTORUN_MAX_STAGES) {
        return AUTORUN_ERR_SIZE_INVALID;
    }

    /* ===== 7. 各阶段基本合法性检查 (可选, 详细模式) ===== */
#ifdef AUTORUN_ENABLE_FULL_VALIDATION
    for (uint32_t i = 0; i < config->stage_count; i++) {
        const stage_config_t *stage = &config->stages[i];

        /* 检查任务数量 */
        if (stage->task_count > AUTORUN_MAX_TASKS_PER_STAGE) {
            return AUTORUN_ERR_SIZE_INVALID;
        }

        /* 检查每个任务的基本字段 */
        for (uint8_t j = 0; j < stage->task_count; j++) {
            const task_config_t *task = &stage->tasks[j];

            /* 任务ID不能全为1 (无效ID) */
            if (task->id == 0xFFFFFFFF) {
                return AUTORUN_ERR_SIZE_INVALID;
            }

            /* 命令字符串不能为空 */
            if (task->command[0] == '\0') {
                return AUTORUN_ERR_SIZE_INVALID;
            }
        }
    }
#endif /* AUTORUN_ENABLE_FULL_VALIDATION */

    return AUTORUN_OK;  /* 所有检查通过 */
}

static int init_result_header(autorun_context_t *ctx)
{
    if (ctx == NULL || ctx->result == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* 清空结果区域 */
    ctx->ops->memset(ctx->result, 0, ctx->result_size);

    /* 设置魔数 */
    ctx->result->magic = AUTORUN_RESULT_MAGIC;

    /* 初始化计数器 */
    ctx->result->total_tasks = 0;
    ctx->result->passed_count = 0;
    ctx->result->failed_count = 0;
    ctx->result->skipped_count = 0;
    ctx->result->overall_status = 0;  /* 初始为PASS */
    ctx->result->completed_stages = 0;

    /* 清空失败任务名 */
    ctx->result->first_failed_task[0] = '\0';
    ctx->result->first_failed_task_id = 0xFFFFFFFF;

    return AUTORUN_OK;
}

static int init_result_array(autorun_context_t *ctx)
{
    if (ctx == NULL || ctx->config == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* 计算总任务数 */
    uint32_t total_tasks = 0;
    for (uint32_t i = 0; i < ctx->config->stage_count; i++) {
        total_tasks += ctx->config->stages[i].task_count;
    }

    /* 检查结果缓冲区是否足够大 */
    size_t required_size = sizeof(autorun_result_t) +
                           total_tasks * sizeof(task_result_t);

    if (required_size > ctx->result_size) {
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Result buffer too small: need %zu bytes, have %u bytes",
               required_size, ctx->result_size);
        return AUTORUN_ERR_NO_MEMORY;
    }

    /* 设置总任务数 */
    ctx->result->total_tasks = total_tasks;

    /* 初始化统计信息 */
    ctx->statistics.total_tasks = total_tasks;
    ctx->statistics.completed_tasks = 0;
    ctx->statistics.passed_tasks = 0;
    ctx->statistics.failed_tasks = 0;
    ctx->statistics.skipped_tasks = 0;

    /* 清空结果数组 */
    if (total_tasks > 0) {
        ctx->ops->memset(ctx->result->results, 0,
                        total_tasks * sizeof(task_result_t));
    }

    ctx_log(ctx, LOG_LEVEL_DEBUG,
           "Result array initialized: %u tasks, buffer size %u bytes",
           total_tasks, ctx->result_size);

    return AUTORUN_OK;
}

static int execute_pre_commands(autorun_context_t *ctx,
                                const stage_config_t *stage)
{
    if (ctx == NULL || stage == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    ctx_log(ctx, LOG_LEVEL_INFO,
           "Executing %u pre-commands for stage '%s'",
           stage->pre_command_count, stage->stage_name);

    for (uint8_t i = 0; i < stage->pre_command_count; i++) {
        const char *cmd = stage->pre_commands[i];
        if (cmd[0] == '\0') {
            continue;  /* 跳过空命令 */
        }

        ctx_log(ctx, LOG_LEVEL_DEBUG,
               "Pre-command[%u]: %s", i, cmd);

        char output[256];  /* 前置命令通常输出较少 */
        int ret = ctx->ops->execute_command(cmd, output, sizeof(output), 30000);

        if (ret != 0) {
            ctx_log(ctx, LOG_LEVEL_ERROR,
                   "Pre-command[%u] failed with code %d: %s", i, ret, cmd);
            return AUTORUN_ERR_EXEC_ERROR;
        }
    }

    return AUTORUN_OK;
}

static int execute_post_commands(autorun_context_t *ctx,
                                 const stage_config_t *stage)
{
    if (ctx == NULL || stage == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    ctx_log(ctx, LOG_LEVEL_INFO,
           "Executing %u post-commands for stage '%s'",
           stage->post_command_count, stage->stage_name);

    for (uint8_t i = 0; i < stage->post_command_count; i++) {
        const char *cmd = stage->post_commands[i];
        if (cmd[0] == '\0') {
            continue;
        }

        ctx_log(ctx, LOG_LEVEL_DEBUG,
               "Post-command[%u]: %s", i, cmd);

        char output[256];
        int ret = ctx->ops->execute_command(cmd, output, sizeof(output), 30000);

        if (ret != 0) {
            ctx_log(ctx, LOG_LEVEL_ERROR,
                   "Post-command[%u] failed with code %d: %s", i, ret, cmd);
            /* 后置命令失败不中止流程，只记录错误 */
        }
    }

    return AUTORUN_OK;
}

static int execute_stage_tasks(autorun_context_t *ctx,
                               const stage_config_t *stage)
{
    if (ctx == NULL || stage == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    int stage_result = AUTORUN_OK;

    for (uint8_t i = 0; i < stage->task_count; i++) {
        /* ===== 检查全局超时 ===== */
        if (check_global_timeout(ctx)) {
            ctx_log(ctx, LOG_LEVEL_ERROR,
                   "Global timeout exceeded during task execution");
            ctx->state = AUTORUN_STATE_ERROR;
            return AUTORUN_ERR_TIMEOUT;
        }

        /* ===== 获取任务配置 ===== */
        const task_config_t *task = &stage->tasks[i];
        ctx->current_task_index = i;

        /* ===== 跳过禁用的任务 ===== */
        if (!task->enabled) {
            ctx_log(ctx, LOG_LEVEL_DEBUG,
                   "Task[%u] '%s' is disabled, skipping",
                   task->id, task->name);

            /* 记录跳过的任务 */
            task_result_t skipped_result;
            ctx->ops->memset(&skipped_result, 0, sizeof(skipped_result));
            skipped_result.task_id = task->id;
            skipped_result.status = TASK_STATUS_SKIPPED;
            skipped_result.timestamp_start = ctx->ops->get_timestamp();
            skipped_result.timestamp_end = skipped_result.timestamp_start;

            /* 写入结果 */
            write_task_result(ctx, ctx->statistics.completed_tasks,
                            NULL, task);  /* NULL表示使用skipped_result */

            ctx->statistics.skipped_tasks++;
            ctx->statistics.completed_tasks++;
            continue;
        }

        /* ===== 触发任务开始回调 ===== */
        if (ctx->on_task_start != NULL) {
            int cb_ret = ctx->on_task_start(ctx, task);
            if (cb_ret != 0) {
                ctx_log(ctx, LOG_LEVEL_INFO,
                       "Task start callback requested skip for '%s'",
                       task->name);

                /* 标记为跳过 */
                task_result_t skipped_result;
                ctx->ops->memset(&skipped_result, 0, sizeof(skipped_result));
                skipped_result.task_id = task->id;
                skipped_result.status = TASK_STATUS_SKIPPED;

                write_task_result(ctx, ctx->statistics.completed_tasks,
                                NULL, task);

                ctx->statistics.skipped_tasks++;
                ctx->statistics.completed_tasks++;
                continue;
            }
        }

        /* ===== 准备执行器输入 ===== */
        executor_input_t exec_input;
        ctx->ops->memset(&exec_input, 0, sizeof(exec_input));
        exec_input.task = task;
        exec_input.ops = ctx->ops;
        exec_input.log_level = ctx->config->global_config.log_level;
        exec_input.capture_full_output = ctx->options.capture_full_output;

        /* 可选: 提供外部输出缓冲区 (如果需要完整输出) */
        if (ctx->options.capture_full_output && task->log_output) {
            /* TODO: 分配更大的缓冲区 */
        }

        /* ===== 执行任务 ===== */
        executor_output_t exec_output;
        ctx->ops->memset(&exec_output, 0, sizeof(exec_output));

        int exec_ret = task_executor_execute(&exec_input, &exec_output);

        /* ===== 写入任务结果 ===== */
        uint32_t result_idx = ctx->statistics.completed_tasks;
        write_task_result(ctx, result_idx, &exec_output, task);

        /* ===== 更新统计信息 ===== */
        update_statistics(ctx, &exec_output, task);

        /* ===== 触发任务完成回调 ===== */
        if (ctx->on_task_complete != NULL) {
            /* 构造标准格式的任务结果用于回调 */
            task_result_t callback_result;
            task_executor_to_task_result(&exec_output, task->id, &callback_result);

            int cb_ret = ctx->on_task_complete(ctx, task, &callback_result);
            if (cb_ret != 0) {
                ctx_log(ctx, LOG_LEVEL_WARN,
                       "Task complete callback requested abort after '%s'",
                       task->name);
                stage_result = AUTORUN_ERR_CRITICAL_FAIL;
                break;  /* 中止后续任务 */
            }
        }

        /* ===== 检查关键任务失败 ===== */
        if (exec_ret != 0 && task->critical) {
            ctx_log(ctx, LOG_LEVEL_ERROR,
                   "!!! CRITICAL task '%s' FAILED, aborting stage !!!",
                   task->name);

            ctx->statistics.last_error_code = AUTORUN_ERR_CRITICAL_FAIL;
            strncpy(ctx->statistics.last_failed_task, task->name,
                    AUTORUN_TASK_NAME_MAX_LEN - 1);

            stage_result = AUTORUN_ERR_CRITICAL_FAIL;

            if (ctx->options.stop_on_critical_fail) {
                break;  /* 中止后续任务 */
            }
        }

        /* ===== 检查非关键任务失败且continue_on_error=false ===== */
        if (exec_ret != 0 && !task->critical &&
            !ctx->config->global_config.continue_on_error) {
            ctx_log(ctx, LOG_LEVEL_ERROR,
                   "Task '%s' failed and continue_on_error=false, aborting",
                   task->name);

            stage_result = exec_ret;
            break;
        }
    }

    return stage_result;
}

static void update_statistics(autorun_context_t *ctx,
                              const executor_output_t *task_output,
                              const task_config_t *task)
{
    if (ctx == NULL || task_output == NULL) {
        return;
    }

    /* 更新当前任务ID */
    ctx->statistics.current_task_id = (int32_t)(task ? task->id : -1);

    /* 根据任务状态更新计数器 */
    switch (task_output->status) {
        case TASK_STATUS_SUCCESS:
            ctx->statistics.passed_tasks++;
            break;

        case TASK_STATUS_FAILED:
        case TASK_STATUS_TIMEOUT:
        case TASK_STATUS_ERROR:
            ctx->statistics.failed_tasks++;
            ctx->statistics.last_error_code =
                (task_output->status == TASK_STATUS_TIMEOUT) ?
                AUTORUN_ERR_TIMEOUT : AUTORUN_ERR_EXEC_ERROR;

            if (task != NULL) {
                strncpy(ctx->statistics.last_failed_task, task->name,
                        AUTORUN_TASK_NAME_MAX_LEN - 1);
            }
            break;

        case TASK_STATUS_SKIPPED:
            ctx->statistics.skipped_tasks++;
            break;

        default:
            break;
    }

    /* 增加已完成任务计数 */
    ctx->statistics.completed_tasks++;
}

static int write_task_result(autorun_context_t *ctx,
                             uint32_t result_index,
                             const executor_output_t *executor_output,
                             const task_config_t *task)
{
    if (ctx == NULL || ctx->result == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    /* 边界检查 */
    if (result_index >= ctx->result->total_tasks) {
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Result index %u out of bounds (total=%u)",
               result_index, ctx->result->total_tasks);
        return AUTORUN_ERR_RESULT_OVERFLOW;
    }

    task_result_t *target = &ctx->result->results[result_index];

    if (executor_output != NULL) {
        /* 从执行器输出转换 */
        task_executor_to_task_result(executor_output,
                                    task ? task->id : 0,
                                    target);
    } else if (task != NULL) {
        /* 构造一个SKIPPED结果 */
        ctx->ops->memset(target, 0, sizeof(task_result_t));
        target->task_id = task->id;
        target->status = TASK_STATUS_SKIPPED;
        target->timestamp_start = ctx->ops->get_timestamp();
        target->timestamp_end = target->timestamp_start;
    }

    return AUTORUN_OK;
}

static void update_result_summary(autorun_context_t *ctx)
{
    if (ctx == NULL || ctx->result == NULL) {
        return;
    }

    /* 从统计信息更新结果头部的汇总数据 */
    ctx->result->passed_count = ctx->statistics.passed_tasks;
    ctx->result->failed_count = ctx->statistics.failed_tasks;
    ctx->result->skipped_count = ctx->statistics.skipped_tasks;

    /* 确定总体状态 */
    uint32_t executed_tasks = ctx->statistics.passed_tasks +
                              ctx->statistics.failed_tasks;

    if (ctx->statistics.failed_tasks > 0) {
        ctx->result->overall_status = 1;  /* FAIL */

        /* 记录第一个失败的任务 (如果还没有) */
        if (ctx->result->first_failed_task_id == 0xFFFFFFFF &&
            ctx->statistics.last_failed_task[0] != '\0') {
            strncpy(ctx->result->first_failed_task,
                    ctx->statistics.last_failed_task,
                    AUTORUN_TASK_NAME_MAX_LEN - 1);
            /* 查找对应的task_id */
            ctx->result->first_failed_task_id = 0;  /* TODO: 查找 */
        }
    } else {
        ctx->result->overall_status = 0;  /* PASS */
    }
}

static bool check_global_timeout(autorun_context_t *ctx)
{
    if (ctx == NULL) {
        return false;
    }

    /* 检查是否启用了全局超时 */
    if (ctx->options.global_timeout_ms == 0) {
        return false;  /* 0 表示无限制 */
    }

    uint32_t current_time = ctx->ops->get_timestamp();
    uint32_t elapsed = current_time - ctx->start_timestamp;

    if (elapsed >= ctx->options.global_timeout_ms) {
        ctx_log(ctx, LOG_LEVEL_ERROR,
               "Global timeout: elapsed=%ums, limit=%ums",
               elapsed, ctx->options.global_timeout_ms);
        return true;
    }

    return false;
}

/*
 * 统一日志宏定义 - 使用 autorun_log 系统替代 ctx_log()
 *
 * 为了向后兼容，保留 ctx_log() 函数但内部调用统一日志系统。
 * 新代码建议直接使用 AUTORUN_LOG_xxx 宏。
 */

/**
 * @brief 兼容性日志函数（内部使用）
 *
 * 将原有的 ctx_log() 调用重定向到统一的 autorun_log 系统。
 * 保持原有接口不变，降低迁移风险。
 */
#define ctx_log(ctx, level, fmt, ...) \
    autorun_log_output((level), fmt, ##__VA_ARGS__)

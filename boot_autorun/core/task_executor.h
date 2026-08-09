/**
 * @file task_executor.h
 * @brief 启动各阶段自动运行框架 - 任务执行器头文件
 *
 * 本文件定义了任务执行器的接口，负责：
 * - 执行单个Shell命令（通过平台操作接口）
 * - 处理超时和重试逻辑
 * - 验证执行结果是否符合预期
 * - 记录详细的执行信息到结果结构体
 *
 * 设计原则:
 * 1. 单一职责: 只关注单个任务的完整生命周期
 * 2. 平台无关: 通过 ops 接口调用平台功能
 * 3. 可观测性: 记录详细的时间戳和输出摘要
 * 4. 容错性: 支持重试机制和超时控制
 *
 * 调用关系:
 *   autorun_core.c (调度器)
 *       └──→ task_executor_execute() (本模块)
 *               ├──→ ops->execute_command() (平台实现)
 *               ├──→ result_checker_*() (结果验证)
 *               └──→ 写入 task_result_t (输出)
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef TASK_EXECUTOR_H
#define TASK_EXECUTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "autorun_config.h"

/* ================================================================== */
/*                         前向声明                                    */
/* ================================================================== */

struct autorun_context_s;
typedef struct autorun_context_s autorun_context_t;

/* ================================================================== */
/*                    执行器配置结构体                                 */
/* ================================================================== */

/**
 * @brief 任务执行器输入参数
 *
 * 封装了执行单个任务所需的所有输入信息。
 * 由调度器(autorun_core.c)在调用前准备。
 */
typedef struct {
    /** @brief 要执行的任务配置 (来自配置数据) */
    const task_config_t *task;

    /** @brief 平台操作函数表 (来自框架上下文) */
    const autorun_platform_ops_t *ops;

    /**
     * @brief 日志级别阈值
     *
     * 只有 >= 此级别的日志才会输出。
     * 来自 global_config.log_level 或 options.verbose_logging
     */
    int log_level;

    /**
     * @brief 是否捕获完整输出
     *
     * true: 将命令的完整stdout/stderr记录到output_summary
     * false: 只记录前 OUTPUT_SUMMARY_MAX_LEN 字符
     */
    bool capture_full_output;

    /**
     * @brief 输出缓冲区指针 (可选, 用于大输出场景)
     *
     * 如果为NULL，使用内部栈缓冲区。
     * 如果非NULL，必须确保有足够空间且调用者负责释放。
     */
    char *external_output_buffer;

    /**
     * @brief 外部缓冲区大小 (bytes)
     *
     * 当 external_output_buffer 非NULL时有效。
     */
    size_t external_buffer_size;
} executor_input_t;

/**
 * @brief 任务执行器输出结果
 *
 * 封装了任务执行的完整结果，包括状态、时间统计等。
 * 由执行器填充，调度器读取并写入最终结果区域。
 */
typedef struct {
    /** @brief 任务执行最终状态 */
    task_status_t status;

    /** @brief Shell命令返回码 */
    int32_t return_code;

    /** @brief 实际执行耗时 (毫秒) */
    uint32_t elapsed_ms;

    /** @brief 任务开始时间戳 (毫秒) */
    uint32_t timestamp_start;

    /** @brief 任务结束时间戳 (毫秒) */
    uint32_t timestamp_end;

    /**
     * @brief 命令输出摘要
     *
     * 包含标准输出的前 N 个字符 (N = AUTORUN_OUTPUT_SUMMARY_MAX_LEN)。
     * 以 '\0' 结尾。
     */
    char output_summary[AUTORUN_OUTPUT_SUMMARY_MAX_LEN];

    /** @brief 实际执行次数 (首次 + 重试) */
    uint8_t attempt_count;

    /** @brief 第一次失败的错误码 (0表示无失败) */
    int32_t first_error_code;

    /**
     * @brief 结果验证详情
     *
     * 记录期望值与实际值的对比，便于调试。
     */
    struct {
        /** @brief 期望结果类型 */
        expect_type_t expected_type;

        /** @brief 是否通过预期检查 */
        bool check_passed;

        /** @brief 失败原因描述 (如果check_passed=false) */
        char failure_reason[128];
    } validation;
} executor_output_t;

/* ================================================================== */
/*                   核心API函数声明                                   */
/* ================================================================== */

/**
 * @brief 执行单个任务
 *
 * 这是任务执行器的核心函数。完成一个任务的完整生命周期：
 * 1. 记录开始时间和状态
 * 2. 循环执行命令 (含重试)
 * 3. 每次执行后验证结果
 * 4. 更新统计信息和输出
 *
 * 典型用法:
 * @code
 *   executor_input_t input = {
 *       .task = &stage->tasks[i],
 *       .ops = ctx->ops,
 *       .log_level = ctx->config->global_config.log_level,
 *       .capture_full_output = ctx->options.capture_full_output,
 *   };
 *   executor_output_t output;
 *   memset(&output, 0, sizeof(output));
 *
 *   int ret = task_executor_execute(&input, &output);
 *   if (ret != 0) {
 *       // 处理错误
 *   }
 *
 *   // 将 output 写入 ctx->result->results[]
 * @endcode
 *
 * @param[in]  input  执行器输入参数 (不能为NULL)
 * @param[out] output 执行器输出结果 (不能为NULL, 调用前应清零)
 *
 * @return int
 *         0 = 任务成功完成 (status == SUCCESS)
 *         AUTORUN_ERR_INVALID_PARAM = 参数无效
 *         AUTORUN_ERR_TIMEOUT = 任务超时
 *         AUTORUN_ERR_EXEC_ERROR = 命令执行错误
 *         其他负值 = 内部错误
 *
 * @note 此函数会处理:
 *       - 禁用的任务 (直接返回 SKIPPED)
 *       - 重试逻辑 (根据 task->retry_count 和 retry_delay_ms)
 *       - 超时检测 (根据 task->timeout_ms)
 *       - 结果验证 (根据 task->expected_result.type)
 *       - 时间戳记录 (start/end/elapsed)
 *
 * @warning 此函数是阻塞式的，对于长耗时任务可能占用较长时间。
 *         在实时系统中需注意对调度的影响。
 */
int task_executor_execute(const executor_input_t *input,
                          executor_output_t *output);

/**
 * @brief 初始化执行器 (全局初始化, 可选)
 *
 * 执行一些一次性初始化工作，如：
 * - 分配内部缓冲区
 * - 初始化正则表达式引擎 (如果需要)
 * - 注册信号处理 (Linux环境)
 *
 * 在大多数嵌入式环境中可以不调用此函数，
 * task_executor_execute() 会按需自动初始化。
 *
 * @return int 0=成功
 *
 * @note 线程安全: 多次调用是安全的 (幂等操作)
 */
int task_executor_init(void);

/**
 * @brief 清理执行器资源 (全局清理, 可选)
 *
 * 释放 task_executor_init() 分配的资源。
 * 在程序退出前调用。
 *
 * @note 不调用也不会造成内存泄漏 (大多数情况下无动态分配)
 */
void task_executor_cleanup(void);

/* ================================================================== */
/*                      结果验证函数                                   */
/* ================================================================== */

/**
 * @brief 验证任务执行结果是否符合预期
 *
 * 根据 expected_result_t 中定义的类型和条件，
 * 检查实际的 return_code 和 output 是否符合要求。
 *
 * 支持的验证类型:
 * - EXPECT_TYPE_RETURN_CODE: 检查返回码
 * - EXPECT_TYPE_STRING_MATCH: 子串匹配
 * - EXPECT_TYPE_REGEX: 正则表达式匹配
 * - EXPECT_TYPE_CHECKSUM: 校验和验证
 * - EXPECT_TYPE_JSON_OUTPUT: JSON解析检查
 * - EXPECT_TYPE_RANGE: 数值范围检查
 *
 * @param[in]  expected 期望结果定义 (不能为NULL)
 * @param[in]  actual_return_code 实际命令返回码
 * @param[in]  actual_output 实际命令输出字符串 (可为NULL如果不需要)
 * @param[out] failure_reason 失败原因描述缓冲区 (可为NULL)
 * @param[in]  reason_size 缓冲区大小
 *
 * @return bool
 *         true = 通过预期检查
 *         false = 未通过 (failure_reason 会包含原因描述)
 *
 * @note 此函数可独立于 task_executor_execute() 使用，
 *       适用于需要自定义执行流程的场景。
 */
bool task_executor_validate_result(const expected_result_t *expected,
                                   int32_t actual_return_code,
                                   const char *actual_output,
                                   char *failure_reason,
                                   size_t reason_size);

/* ================================================================== */
/*                     工具函数                                       */
/* ================================================================== */

/**
 * @brief 将 executor_output_t 转换为 task_result_t
 *
 * 将执行器的内部输出格式转换为标准的任务结果格式，
 * 用于写入最终的结果区域。
 *
 * @param[in]  executor_output 执行器输出
 * @param[in]  task_id 任务ID (来自 task_config_t.id)
 * @param[out] task_result 标准任务结果 (输出)
 */
void task_executor_to_task_result(const executor_output_t *executor_output,
                                  uint32_t task_id,
                                  task_result_t *task_result);

/**
 * @brief 获取执行器版本信息
 *
 * @return const char* 版本字符串 (如 "1.0.0")
 */
const char* task_executor_get_version(void);

/**
 * @brief 获取执行器支持的最大特性列表
 *
 * 返回当前编译环境下执行器支持的特性标志位。
 * 可用于运行时特性检测。
 *
 * @return uint32_t 特性位图:
 *         bit 0: 支持正则表达式
 *         bit 1: 支持JSON解析
 *         bit 2: 支持校验和计算 (CRC32/MD5/SHA256)
 *         bit 3: 支持外部输出缓冲区
 *         bit 4-31: 保留
 */
uint32_t task_executor_get_capabilities(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_EXECUTOR_H */

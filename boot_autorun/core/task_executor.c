/**
 * @file task_executor.c
 * @brief 启动各阶段自动运行框架 - 任务执行器实现
 *
 * 本文件实现了任务执行器的核心功能，包括：
 * - Shell命令的执行与输出捕获
 * - 超时检测与重试机制
 * - 多种期望结果的验证逻辑
 * - 执行过程的详细记录
 *
 * 实现特点:
 * 1. 平台无关: 所有平台相关操作通过 autorun_platform_ops_t 接口
 * 2. 资源安全: 严格的边界检查和错误处理
 * 3. 可配置性: 支持通过 input 参数定制行为
 * 4. 可观测性: 详细的时间戳和状态记录
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "task_executor.h"
#include "autorun_core.h"
#include "../utils/autorun_log.h"

/* ================================================================== */
/*                         内部宏定义                                  */
/* ================================================================== */

/**
 * @brief 内部输出缓冲区大小 (栈上分配)
 *
 * 用于捕获命令输出的临时缓冲区。
 * 大小应 >= AUTORUN_OUTPUT_SUMMARY_MAX_LEN，以便完整记录摘要。
 *
 * 选择4KB的原因:
 * - 足够大多数命令的输出（通常 < 1KB）
 * - 栈空间可承受（BL1环境可能需要减小）
 * - 避免频繁的堆分配
 */
#define INTERNAL_OUTPUT_BUFFER_SIZE   4096

/**
 * @brief 默认重试间隔 (毫秒)
 *
 * 当 task->retry_delay_ms = 0 时使用此默认值。
 */
#define DEFAULT_RETRY_DELAY_MS        1000

/**
 * @brief 最小有效超时时间 (毫秒)
 *
 * 防止设置过小的超时值导致立即失败。
 */
#define MIN_TIMEOUT_MS                100

/* ================================================================== */
/*                    内部辅助函数声明                                 */
/* ================================================================== */

/**
 * @brief 验证返回码是否符合预期
 */
static bool validate_return_code(const expected_result_t *expected,
                                 int32_t actual_code,
                                 char *reason, size_t reason_size);

/**
 * @brief 验证输出是否包含指定子串
 */
static bool validate_string_match(const expected_result_t *expected,
                                  const char *output,
                                  char *reason, size_t reason_size);

/**
 * @brief 验证输出是否匹配正则表达式
 */
static bool validate_regex(const expected_result_t *expected,
                           const char *output,
                           char *reason, size_t reason_size);

/**
 * @brief 验证校验和是否正确
 */
static bool validate_checksum(const expected_result_t *expected,
                              const char *output,
                              char *reason, size_t reason_size);

/**
 * @brief 验证JSON输出是否符合预期
 */
static bool validate_json_output(const expected_result_t *expected,
                                 const char *output,
                                 char *reason, size_t reason_size);

/**
 * @brief 验证数值是否在范围内
 */
static bool validate_range(const expected_result_t *expected,
                           int32_t actual_code,
                           const char *output,
                           char *reason, size_t reason_size);

/**
 * @brief 安全地拷贝输出摘要到缓冲区
 */
static void safe_copy_output_summary(char *dest,
                                     size_t dest_size,
                                     const char *src);

/**
 * @brief 记录执行日志
 */
static void log_execution(const executor_input_t *input,
                          int level,
                          const char *task_name,
                          const char *fmt, ...);

/* ================================================================== */
/*                   全局状态变量 (可选)                               */
/* ================================================================== */

/** @brief 执行器是否已初始化 */
static bool g_executor_initialized = false;

/* ================================================================== */
/*                 公开API函数实现                                     */
/* ================================================================== */

int task_executor_init(void)
{
    /* 幂等操作：多次调用是安全的 */
    if (g_executor_initialized) {
        return AUTORUN_OK;
    }

    /*
     * 在这里可以添加一次性初始化工作：
     * - 分配全局缓冲区
     * - 初始化正则表达式库
     * - 注册信号处理器 (Linux环境)
     *
     * 当前版本不需要动态资源，所以此函数主要是为未来扩展预留。
     */

    g_executor_initialized = true;
    return AUTORUN_OK;
}

void task_executor_cleanup(void)
{
    /*
     * 清理 task_executor_init() 分配的资源。
     * 当前版本无动态资源，此函数为空实现。
     */
    g_executor_initialized = false;
}

int task_executor_execute(const executor_input_t *input,
                          executor_output_t *output)
{
    /* ===== 参数校验 ===== */
    if (input == NULL || output == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (input->task == NULL || input->ops == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    if (input->ops->execute_command == NULL ||
        input->ops->get_timestamp == NULL ||
        input->ops->delay_ms == NULL ||
        input->ops->log_printf == NULL) {
        return AUTORUN_ERR_INVALID_PARAM;
    }

    const task_config_t *task = input->task;
    const autorun_platform_ops_t *ops = input->ops;

    /* ===== 检查任务是否启用 ===== */
    if (!task->enabled) {
        output->status = TASK_STATUS_SKIPPED;
        output->return_code = 0;
        output->elapsed_ms = 0;
        output->attempt_count = 0;
        output->timestamp_start = ops->get_timestamp();
        output->timestamp_end = output->timestamp_start;
        safe_copy_output_summary(output->output_summary,
                                sizeof(output->output_summary),
                                "(task disabled)");

        log_execution(input, LOG_LEVEL_INFO, task->name,
                     "Task SKIPPED (disabled)");
        return AUTORUN_OK;  /* 跳过不算错误 */
    }

    /* ===== 初始化输出结构 ===== */
    output->status = TASK_STATUS_PENDING;
    output->return_code = 0;
    output->elapsed_ms = 0;
    output->attempt_count = 0;
    output->first_error_code = 0;
    output->validation.expected_type = task->expected_result.type;
    output->validation.check_passed = false;
    output->failure_reason[0] = '\0';

    /* ===== 准备输出缓冲区 ===== */
    char *output_buffer;
    size_t buffer_size;

    if (input->external_output_buffer != NULL &&
        input->external_buffer_size > 0) {
        /* 使用外部提供的缓冲区 */
        output_buffer = input->external_output_buffer;
        buffer_size = input->external_buffer_size;
    } else {
        /* 使用内部栈缓冲区 */
        static char internal_buffer[INTERNAL_OUTPUT_BUFFER_SIZE];
        output_buffer = internal_buffer;
        buffer_size = sizeof(internal_buffer);
    }

    /* 清空输出缓冲区 */
    ops->memset(output_buffer, 0, buffer_size);

    /* ===== 记录开始时间 ===== */
    output->timestamp_start = ops->get_timestamp();
    uint32_t stage_start_time = output->timestamp_start;

    log_execution(input, LOG_LEVEL_INFO, task->name,
                 "Task STARTED (timeout=%ums, retry=%u)",
                 task->timeout_ms, task->retry_count);

    /* ===== 确定实际超时值 ===== */
    uint32_t timeout_ms = task->timeout_ms;
    if (timeout_ms > 0 && timeout_ms < MIN_TIMEOUT_MS) {
        timeout_ms = MIN_TIMEOUT_MS;  /* 强制最小超时 */
    }

    /* ===== 执行循环 (含重试) ===== */
    int32_t ret_code = 0;
    bool success = false;
    uint8_t attempt = 0;
    uint8_t max_attempts = 1 + task->retry_count;  /* 首次 + 重试 */

    do {
        attempt++;
        output->attempt_count = attempt;

        /* 更新状态为 RUNNING */
        output->status = TASK_STATUS_RUNNING;

        log_execution(input, LOG_LEVEL_DEBUG, task->name,
                     "Attempt %u/%u: executing command...",
                     attempt, max_attempts);

        /* ===== 执行Shell命令 ===== */
        ret_code = ops->execute_command(
            task->command,
            output_buffer,
            buffer_size,
            timeout_ms
        );

        output->return_code = ret_code;

        /* ===== 验证结果 ===== */
        char failure_reason[128];
        failure_reason[0] = '\0';

        success = task_executor_validate_result(
            &task->expected_result,
            ret_code,
            output_buffer,
            failure_reason,
            sizeof(failure_reason)
        );

        if (success) {
            /* 任务成功完成 */
            output->status = TASK_STATUS_SUCCESS;
            output->validation.check_passed = true;

            log_execution(input, LOG_LEVEL_INFO, task->name,
                         "Task PASSED on attempt %u/%u",
                         attempt, max_attempts);

            break;  /* 退出循环 */
        } else {
            /* 本次尝试失败 */
            output->validation.check_passed = false;

            /* 记录第一次失败的错误码 */
            if (output->first_error_code == 0) {
                output->first_error_code = ret_code;
            }

            /* 复制失败原因 */
            if (failure_reason[0] != '\0') {
                size_t copy_len = strlen(failure_reason);
                if (copy_len > sizeof(output->validation.failure_reason) - 1) {
                    copy_len = sizeof(output->validation.failure_reason) - 1;
                }
                ops->memcpy(output->validation.failure_reason,
                           failure_reason, copy_len);
                output->validation.failure_reason[copy_len] = '\0';
            }

            log_execution(input, LOG_LEVEL_WARN, task->name,
                         "Task FAILED on attempt %u/%u (ret=%d): %s",
                         attempt, max_attempts, ret_code,
                         failure_reason[0] ? failure_reason : "unknown");

            /* 检查是否还有重试机会 */
            if (attempt < max_attempts) {
                /* 计算重试延迟 */
                uint32_t delay = task->retry_delay_ms;
                if (delay == 0) {
                    delay = DEFAULT_RETRY_DELAY_MS;
                }

                log_execution(input, LOG_LEVEL_INFO, task->name,
                             "Retrying in %ums...", delay);

                ops->delay_ms(delay);

                /* 清空输出缓冲区，准备下一次尝试 */
                ops->memset(output_buffer, 0, buffer_size);
            }
        }

    } while (attempt < max_attempts);

    /* ===== 确定最终状态 ===== */
    if (!success) {
        /* 所有的尝试都失败了 */
        if (output->return_code == -1 || output->return_code == -2) {
            /* 特殊返回码表示超时或执行错误 */
            output->status = TASK_STATUS_TIMEOUT;
        } else {
            output->status = TASK_STATUS_FAILED;
        }
    }

    /* ===== 记录结束时间和耗时 ===== */
    output->timestamp_end = ops->get_timestamp();
    output->elapsed_ms = output->timestamp_end - output->timestamp_start;

    /* ===== 拷贝输出摘要 ===== */
    safe_copy_output_summary(output->output_summary,
                            sizeof(output->output_summary),
                            output_buffer);

    /* ===== 日志汇总 ===== */
    const char *status_str = autorun_task_status_string(output->status);
    log_execution(input, LOG_LEVEL_INFO, task->name,
                 "Task COMPLETED: status=%s, elapsed=%ums, attempts=%u",
                 status_str, output->elapsed_ms, output->attempt_count);

    return (output->status == TASK_STATUS_SUCCESS) ? AUTORUN_OK : -1;
}

bool task_executor_validate_result(const expected_result_t *expected,
                                   int32_t actual_return_code,
                                   const char *actual_output,
                                   char *failure_reason,
                                   size_t reason_size)
{
    /* 参数校验 */
    if (expected == NULL) {
        return false;
    }

    if (failure_reason != NULL && reason_size > 0) {
        failure_reason[0] = '\0';
    }

    /* 根据期望结果类型分发验证逻辑 */
    switch (expected->type) {
        case EXPECT_TYPE_RETURN_CODE:
            return validate_return_code(expected, actual_return_code,
                                       failure_reason, reason_size);

        case EXPECT_TYPE_STRING_MATCH:
            return validate_string_match(expected, actual_output,
                                        failure_reason, reason_size);

        case EXPECT_TYPE_REGEX:
            return validate_regex(expected, actual_output,
                                 failure_reason, reason_size);

        case EXPECT_TYPE_CHECKSUM:
            return validate_checksum(expected, actual_output,
                                    failure_reason, reason_size);

        case EXPECT_TYPE_JSON_OUTPUT:
            return validate_json_output(expected, actual_output,
                                       failure_reason, reason_size);

        case EXPECT_TYPE_RANGE:
            return validate_range(expected, actual_return_code, actual_output,
                                 failure_reason, reason_size);

        default:
            /* 未知的期望类型 */
            if (failure_reason != NULL && reason_size > 0) {
                snprintf(failure_reason, reason_size,
                        "Unknown expect type: %d", expected->type);
            }
            return false;
    }
}

void task_executor_to_task_result(const executor_output_t *executor_output,
                                  uint32_t task_id,
                                  task_result_t *task_result)
{
    if (executor_output == NULL || task_result == NULL) {
        return;
    }

    /* 基本信息拷贝 */
    task_result->task_id = task_id;
    task_result->status = executor_output->status;
    task_result->return_code = executor_output->return_code;
    task_result->elapsed_ms = executor_output->elapsed_ms;
    task_result->timestamp_start = executor_output->timestamp_start;
    task_result->timestamp_end = executor_output->timestamp_end;
    task_result->attempt_count = executor_output->attempt_count;
    task_result->first_error_code = executor_output->first_error_code;

    /* 输出摘要拷贝 */
    safe_copy_output_summary(task_result->output_summary,
                            sizeof(task_result->output_summary),
                            executor_output->output_summary);
}

const char* task_executor_get_version(void)
{
    return "1.0.0";
}

uint32_t task_executor_get_capabilities(void)
{
    uint32_t caps = 0;

    /* bit 0: 正则表达式支持 */
#ifdef AUTORUN_ENABLE_REGEX
    caps |= (1 << 0);
#endif

    /* bit 1: JSON解析支持 */
#ifdef AUTORUN_ENABLE_JSON
    caps |= (1 << 1);
#endif

    /* bit 2: 校验和计算支持 (CRC32/MD5/SHA256) */
    caps |= (1 << 2);  /* 始终支持基本CRC32 */

    /* bit 3: 外部输出缓冲区支持 */
    caps |= (1 << 3);  /* 始终支持 */

    return caps;
}

/* ================================================================== */
/*               内部验证函数实现                                      */
/* ================================================================== */

static bool validate_return_code(const expected_result_t *expected,
                                 int32_t actual_code,
                                 char *reason, size_t reason_size)
{
    int32_t expected_code = expected->return_code.expected_code;

    if (actual_code == expected_code) {
        return true;
    }

    if (reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "Return code mismatch: expected %d, got %d",
                expected_code, actual_code);
    }

    return false;
}

static bool validate_string_match(const expected_result_t *expected,
                                  const char *output,
                                  char *reason, size_t reason_size)
{
    /* 如果没有输出，无法进行字符串匹配 */
    if (output == NULL || output[0] == '\0') {
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "No output to match against pattern");
        }
        return false;
    }

    const char *pattern = expected->string_match.pattern;

    /* 使用简单的子串查找 (strstr) */
    if (strstr(output, pattern) != NULL) {
        return true;
    }

    if (reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "Pattern '%s' not found in output", pattern);
    }

    return false;
}

static bool validate_regex(const expected_result_t *expected,
                           const char *output,
                           char *reason, size_t reason_size)
{
#ifdef AUTORUN_ENABLE_REGEX
    /*
     * 这里应该调用正则表达式库进行匹配。
     * 例如 POSIX regex (regcomp/regexec) 或 PCRE 库。
     *
     * 示例代码 (POSIX):
     * regex_t regex;
     * int ret = regcomp(&regex, expected->regex.pattern, REG_EXTENDED);
     * if (ret != 0) { ... }
     * ret = regexec(&regex, output, 0, NULL, 0);
     * regfree(&regex);
     * return (ret == 0);
     */

    /* TODO: 实现正则表达式匹配 */
    if (reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "Regex matching not implemented");
    }
    return false;

#else
    /*
     * 未启用正则表达式支持时，降级为简单的通配符匹配。
     * 支持 * (任意字符序列) 和 ? (单个字符) 通配符。
     */

    if (output == NULL || output[0] == '\0') {
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "No output for regex/wildcard match");
        }
        return false;
    }

    const char *pattern = expected->regex.pattern;
    const char *str = output;
    const char *pat = pattern;

    while (*pat != '\0') {
        if (*pat == '*') {
            /* 匹配任意字符序列 */
            pat++;
            if (*pat == '\0') {
                return true;  /* 尾部的 * 匹配所有剩余字符 */
            }
            /* 查找下一个非通配符字符 */
            while (*str != '\0') {
                if (*str == *pat) {
                    /* 递归检查剩余部分 */
                    if (validate_regex(expected, str, reason, reason_size)) {
                        return true;
                    }
                }
                str++;
            }
            return false;
        } else if (*pat == '?') {
            /* 匹配单个任意字符 */
            if (*str == '\0') {
                break;
            }
            pat++;
            str++;
        } else {
            /* 精确匹配 */
            if (*str != *pat) {
                break;
            }
            pat++;
            str++;
        }
    }

    bool matched = (*pat == '\0' && *str == '\0');

    if (!matched && reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "Pattern '%s' does not match output", pattern);
    }

    return matched;
#endif
}

static bool validate_checksum(const expected_result_t *expected,
                              const char *output,
                              char *reason, size_t reason_size)
{
    /*
     * 校验和验证逻辑:
     * 1. 根据 algorithm 选择算法
     * 2. 对 data_source 指定的数据计算校验和
     *    - data_source == "stdout": 对命令输出计算
     *    - 否则: 将其作为文件路径，读取文件内容并计算
     * 3. 与 expected_value 比较
     */

    checksum_alg_t alg = expected->checksum.algorithm;
    uint32_t expected_value = expected->checksum.expected_value;
    const char *data_source = expected->checksum.data_source;

    /* 确定要计算校验和的数据 */
    const char *data = NULL;
    size_t data_len = 0;

    if (strncmp(data_source, "stdout", 6) == 0) {
        /* 对命令输出计算校验和 */
        data = output;
        data_len = (output != NULL) ? strlen(output) : 0;
    } else {
        /*
         * TODO: 从文件读取数据
         * 需要文件系统支持，在BL1/BL31等环境中可能不可用。
         * 可以通过 ops 接口扩展文件读取功能。
         */
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "File-based checksum not supported yet: %s",
                    data_source);
        }
        return false;
    }

    if (data == NULL || data_len == 0) {
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "No data available for checksum calculation");
        }
        return false;
    }

    /* 计算校验和 */
    uint32_t calculated_crc = 0;

    switch (alg) {
        case CHECKSUM_ALG_CRC32:
            calculated_crc = autorun_calc_crc32((const uint8_t *)data, data_len);
            break;

        case CHECKSUM_ALG_CRC16:
            /* TODO: CRC16 计算 */
            if (reason != NULL && reason_size > 0) {
                snprintf(reason, reason_size,
                        "CRC16 algorithm not implemented");
            }
            return false;

        case CHECKSUM_ALG_XOR:
            /* 简单XOR校验 */
            calculated_crc = 0;
            for (size_t i = 0; i < data_len; i++) {
                calculated_crc ^= (uint8_t)data[i];
            }
            calculated_crc &= 0xFFFFFFFF;
            break;

        case CHECKSUM_ALG_MD5:
        case CHECKSUM_ALG_SHA256:
            /* 需要密码库支持 */
            if (reason != NULL && reason_size > 0) {
                snprintf(reason, reason_size,
                        "%s algorithm not available (needs crypto library)",
                        (alg == CHECKSUM_ALG_MD5) ? "MD5" : "SHA256");
            }
            return false;

        default:
            if (reason != NULL && reason_size > 0) {
                snprintf(reason, reason_size,
                        "Unknown checksum algorithm: %d", alg);
            }
            return false;
    }

    /* 比较校验和 */
    if (calculated_crc == expected_value) {
        return true;
    }

    if (reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "Checksum mismatch: expected 0x%08X, got 0x%08X (algorithm=%d)",
                expected_value, calculated_crc, alg);
    }

    return false;
}

static bool validate_json_output(const expected_result_t *expected,
                                 const char *output,
                                 char *reason, size_t reason_size)
{
#ifdef AUTORUN_ENABLE_JSON
    /*
     * JSON解析验证逻辑:
     * 1. 解析输出字符串为JSON对象
     * 2. 检查 check_keys[] 中列出的key是否存在
     * 3. 如果 value_conditions[].enabled=true，验证对应key的值
     *
     * 需要JSON解析库支持 (如 cJSON, jansson 等)
     */

    /* TODO: 实现JSON解析和验证 */
    if (reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "JSON validation not implemented");
    }
    return false;

#else
    /* 未启用JSON支持时，使用简单的文本搜索 */
    if (output == NULL || output[0] == '\0') {
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "No output for JSON validation");
        }
        return false;
    }

    /*
     * 降级方案: 检查输出中是否包含所有指定的key名称。
     * 这不是真正的JSON解析，但可以用于简单场景。
     */
    const char (*check_keys)[AUTORUN_JSON_KEY_MAX_LEN] =
        expected->json_output.check_keys;
    uint8_t key_count = expected->json_output.key_count;

    if (key_count == 0 || key_count > AUTORUN_MAX_CHECK_KEYS) {
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "Invalid key count: %u", key_count);
        }
        return false;
    }

    for (uint8_t i = 0; i < key_count; i++) {
        if (strstr(output, check_keys[i]) == NULL) {
            if (reason != NULL && reason_size > 0) {
                snprintf(reason, reason_size,
                        "JSON key '%s' not found in output", check_keys[i]);
            }
            return false;
        }
    }

    return true;
#endif
}

static bool validate_range(const expected_result_t *expected,
                           int32_t actual_code,
                           const char *output,
                           char *reason, size_t reason_size)
{
    int32_t min_val = expected->range.min_val;
    int32_t max_val = expected->range.max_val;
    const char *extract_regex = expected->range.extract_regex;

    int32_t value_to_check;

    /* 确定要检查的数值 */
    if (extract_regex[0] != '\0' && output != NULL) {
        /*
         * TODO: 使用正则表达式从输出中提取数值
         * 例如: extract_regex = "speed=(\\d+)"
         *       从 "speed=1234 Mbps" 中提取 1234
         */
#ifdef AUTORUN_ENABLE_REGEX
        /* 实现正则提取 */
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "Numeric extraction from output not implemented");
        }
        return false;
#else
        if (reason != NULL && reason_size > 0) {
            snprintf(reason, reason_size,
                    "Regex extraction not available (need ENABLE_REGEX)");
        }
        return false;
#endif
    } else {
        /* 使用返回码作为检查值 */
        value_to_check = actual_code;
    }

    /* 范围检查 */
    if (value_to_check >= min_val && value_to_check <= max_val) {
        return true;
    }

    if (reason != NULL && reason_size > 0) {
        snprintf(reason, reason_size,
                "Value %d out of range [%d, %d]",
                value_to_check, min_val, max_val);
    }

    return false;
}

/* ================================================================== */
/*                  内部工具函数实现                                   */
/* ================================================================== */

static void safe_copy_output_summary(char *dest,
                                     size_t dest_size,
                                     const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    /* 计算可拷贝的最大长度 (保留空间给终止符) */
    size_t max_copy = dest_size - 1;
    size_t src_len = strlen(src);

    if (src_len <= max_copy) {
        /* 源字符串完全放入目标缓冲区 */
        memcpy(dest, src, src_len);
        dest[src_len] = '\0';
    } else {
        /* 截断源字符串，保留最后几个字符显示 "..." */
        size_t ellipsis_len = 3;
        if (max_copy > ellipsis_len) {
            memcpy(dest, src, max_copy - ellipsis_len);
            dest[max_copy - ellipsis_len] = '.';
            dest[max_copy - ellipsis_len + 1] = '.';
            dest[max_copy - ellipsis_len + 2] = '.';
        } else {
            memcpy(dest, src, max_copy);
        }
        dest[max_copy] = '\0';
    }
}

/*
 * 统一日志宏定义 - 使用 autorun_log 系统替代 log_execution()
 *
 * 将原有的 log_execution() 调用重定向到统一的 autorun_log 系统。
 * 自动添加任务名前缀，保持原有日志格式。
 */

/**
 * @brief 兼容性任务日志函数（内部使用）
 *
 * 将原有的 log_execution() 调用重定向到统一日志系统。
 * 保持 [Task:xxx] 前缀格式不变。
 */
#define log_execution(input, level, task_name, fmt, ...) \
    do { \
        if ((level) <= (input)->log_level) { \
            autorun_log_output((level), "[Task:%s] " fmt, \
                              (task_name), ##__VA_ARGS__); \
        } \
    } while (0)

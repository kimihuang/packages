/**
 * @file autorun_core.h
 * @brief 启动各阶段自动运行框架 - 核心引擎头文件
 *
 * 本文件定义了自动运行框架的核心API，包括：
 * - 框架初始化与配置加载
 * - 阶段任务调度与执行
 * - 结果收集与状态查询
 * - 生命周期管理
 *
 * 核心设计原则:
 * 1. 平台无关: 通过 autorun_platform_ops_t 接口适配不同环境
 * 2. 资源安全: 所有操作有边界检查和错误处理
 * 3. 可观测性: 提供详细日志和状态回调
 * 4. 可恢复性: 支持断点续跑和异常恢复
 *
 * 使用流程:
 *   1. autorun_init()        - 初始化框架并加载配置
 *   2. autorun_run_stage()    - 执行当前阶段的所有任务
 *   3. autorun_get_result()  - 获取执行结果
 *   4. autorun_cleanup()     - 清理资源 (可选)
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef AUTORUN_CORE_H
#define AUTORUN_CORE_H

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
/*                    平台操作函数表类型                              */
/* ================================================================== */

/**
 * @brief 日志输出函数类型
 *
 * @param level  日志级别 (0-5, 见 log_level_t)
 * @param fmt    格式字符串 (同 printf)
 */
typedef void (*autorun_log_fn_t)(int level, const char *fmt, ...);

/**
 * @brief Shell命令执行函数类型
 *
 * @param command     要执行的命令字符串
 * @param output      输出缓冲区 (存储命令的标准输出)
 * @param output_size 输出缓冲区大小 (bytes)
 * @param timeout_ms  超时时间 (毫秒), 0表示无限等待
 * @return int 命令返回码 (0=成功, 非0=失败)
 */
typedef int (*autorun_exec_fn_t)(const char *command, char *output,
                                 size_t output_size, uint32_t timeout_ms);

/**
 * @brief 获取系统时间戳函数类型
 *
 * @return uint32_t 当前时间戳 (毫秒)
 *               建议从系统启动或固定参考点开始计算
 */
typedef uint32_t (*autorun_timestamp_fn_t)(void);

/**
 * @brief 延时函数类型
 *
 * @param ms 延时毫秒数
 */
typedef void (*autorun_delay_fn_t)(uint32_t ms);

/**
 * @brief 内存拷贝函数类型
 *
 * @param dest 目标地址
 * @param src  源地址
 * @param n    拷贝字节数
 * @return void* dest
 */
typedef void *(*autorun_memcpy_fn_t)(void *dest, const void *src, size_t n);

/**
 * @brief 内存清零函数类型
 *
 * @param s   内存起始地址
 * @param c   填充值 (通常为0)
 * @param n   字节数
 * @return void* s
 */
typedef void *(*autorun_memset_fn_t)(void *s, int c, size_t n);

/**
 * @brief 平台操作函数表
 *
 * 每个启动阶段必须实现此接口，以适配各自的运行环境。
 * 框架通过此接口调用平台相关功能，实现平台无关的核心逻辑。
 */
typedef struct {
    /** 日志输出 */
    autorun_log_fn_t         log_printf;
    
    /** 执行Shell命令 */
    autorun_exec_fn_t        execute_command;
    
    /** 获取当前时间戳 (毫秒) */
    autorun_timestamp_fn_t   get_timestamp;
    
    /** 毫秒级延时 */
    autorun_delay_fn_t       delay_ms;
    
    /** 内存拷贝 (需处理Cache一致性) */
    autorun_memcpy_fn_t      memcpy;
    
    /** 内存清零 */
    autorun_memset_fn_t      memset;
} autorun_platform_ops_t;

/* ================================================================== */
/*                     回调函数类型                                  */
/* ================================================================== */

/**
 * @brief 任务开始执行回调
 *
 * @param ctx       框架上下文
 * @param task      即将执行的任务配置
 * @return int 继续执行(0) 或跳过此任务(非0)
 */
typedef int (*autorun_task_start_cb_t)(autorun_context_t *ctx,
                                      const task_config_t *task);

/**
 * @brief 任务完成回调
 *
 * @param ctx       框架上下文
 * @param task      已完成的任务配置
 * @param result    执行结果
 * @return int 继续后续任务(0) 或中止阶段(非0)
 */
typedef int (*autorun_task_complete_cb_t)(autorun_context_t *ctx,
                                        const task_config_t *task,
                                        const task_result_t *result);

/**
 * @brief 阶段开始回调
 *
 * @param ctx       框架上下文
 * @param stage     即将执行的阶段配置
 * @return int 继续执行(0) 或跳过此阶段(非0)
 */
typedef int (*autorun_stage_start_cb_t)(autorun_context_t *ctx,
                                       const stage_config_t *stage);

/**
 * @brief 阶段完成回调
 *
 * @param ctx       框架上下文
 * @param stage     已完成的阶段配置
 * @param result    阶段结果汇总
 * @return int 继续下一阶段(0) 或中止全部(非0)
 */
typedef int (*autorun_stage_complete_cb_t)(autorun_context_t *ctx,
                                         const stage_config_t *stage,
                                         const autorun_result_t *result);

/* ================================================================== */
/*                   核心上下文结构体                                */
/* ================================================================== */

/**
 * @brief 框架运行状态枚举
 */
typedef enum {
    /** 未初始化 */
    AUTORUN_STATE_UNINITIALIZED = 0,
    /** 已初始化，等待执行 */
    AUTORUN_STATE_READY,
    /** 正在执行某个阶段 */
    AUTORUN_STATE_RUNNING,
    /** 阶段暂停 (预留) */
    AUTORUN_STATE_PAUSED,
    /** 全部完成 */
    AUTORUN_STATE_COMPLETED,
    /** 发生错误 */
    AUTORUN_STATE_ERROR,
    /** 已清理 */
    AUTORUN_STATE_CLEANED
} autorun_state_t;

/**
 * @brief 框架统计信息
 */
typedef struct {
    /** 总任务数 */
    uint32_t total_tasks;
    
    /** 已完成任务数 */
    uint32_t completed_tasks;
    
    /** 成功任务数 */
    uint32_t passed_tasks;
    
    /** 失败任务数 */
    uint32_t failed_tasks;
    
    /** 跳过任务数 */
    uint32_t skipped_tasks;
    
    /** 总执行耗时 (毫秒) */
    uint32_t total_elapsed_ms;
    
    /** 当前正在执行的任务ID (-1表示无) */
    int32_t current_task_id;
    
    /** 最后一次错误码 */
    int last_error_code;
    
    /** 最后失败的任务名称 */
    char last_failed_task[AUTORUN_TASK_NAME_MAX_LEN];
} autorun_statistics_t;

/**
 * @brief 框架配置选项
 */
typedef struct {
    /** 是否在关键任务失败时立即停止 */
    bool stop_on_critical_fail;
    
    /** 是否记录每个任务的完整输出 */
    bool capture_full_output;
    
    /** 全局超时时间 (毫秒), 0=无限制 */
    uint32_t global_timeout_ms;
    
    /** 结果区域最大大小 (bytes) */
    uint32_t result_buffer_size;
    
    /** 是否启用详细日志 */
    bool verbose_logging;
} autorun_options_t;

/**
 * @brief 自动运行框架上下文
 *
 * 封装了框架的所有运行时状态。
 * 每个实例对应一次完整的自动运行过程。
 */
struct autorun_context_s {
    /* ===== 配置信息 ===== */
    
    /** 原始配置数据指针 (指向内存中的配置区域) */
    const autorun_config_t *config;
    
    /** 配置数据有效性标记 */
    bool config_validated;
    
    /* ===== 运行状态 ===== */
    
    /** 当前框架状态 */
    autorun_state_t state;
    
    /** 当前执行的阶段索引 (-1=未开始) */
    int current_stage_index;
    
    /** 当前执行的任务索引 (-1=无) */
    int current_task_index;
    
    /* ===== 平台接口 ===== */
    
    /** 平台操作函数表 */
    const autorun_platform_ops_t *ops;
    
    /* ===== 结果存储 ===== */
    
    /** 结果数据指针 (指向预分配的结果区域) */
    autorun_result_t *result;
    
    /** 结果区域大小 */
    uint32_t result_size;
    
    /* ===== 统计信息 ===== */
    
    /** 运行统计 */
    autorun_statistics_t statistics;
    
    /* ===== 配置选项 ===== */
    
    /** 运行选项 */
    autorun_options_t options;
    
    /* ===== 回调函数 ===== */
    
    /** 任务开始回调 */
    autorun_task_start_cb_t on_task_start;
    
    /** 任务完成回调 */
    autorun_task_complete_cb_t on_task_complete;
    
    /** 阶段开始回调 */
    autorun_stage_start_cb_t on_stage_start;
    
    /** 阶段完成回调 */
    autorun_stage_complete_cb_t on_stage_complete;
    
    /* ===== 内部使用 ===== */
    
    /** 框架启动时间戳 */
    uint32_t start_timestamp;
    
    /** 当前阶段开始时间戳 */
    uint32_t stage_start_timestamp;
    
    /** 保留字段 */
    uint8_t reserved[32];
};

/* ================================================================== */
/*                      核心API函数声明                               */
/* ================================================================== */

/**
 * @brief 初始化自动运行框架
 *
 * 加载并验证配置数据，初始化内部状态，
 * 准备好执行环境但不立即开始执行。
 *
 * @param[in]  config_addr  配置数据在内存中的物理地址
 * @param[in]  result_addr 结果输出区域的物理地址
 * @param[in]  result_size 结果区域最大大小 (bytes)
 * @param[in]  ops         平台操作函数表 (不能为NULL)
 * @param[out] ctx_out     初始化后的上下文指针 (不能为NULL)
 *
 * @return int
 *         0 = 成功
 *         AUTORUN_ERR_INVALID_PARAM = 参数无效
 *         AUTORUN_ERR_INVALID_MAGIC = 魔数不匹配
 *         AUTORUN_ERR_VERSION_MISMATCH = 版本不兼容
 *         AUTORUN_ERR_CRC_ERROR = CRC校验失败
 *         AUTORUN_ERR_NO_MEMORY = 内存不足
 *
 * @note 此函数会:
 *       1. 校验配置魔数和版本号
 *       2. 计算并验证CRC32
 *       3. 初始化结果区域
 *       4. 设置初始状态为 READY
 *
 * @warning 必须在使用其他API之前调用此函数
 */
int autorun_init(uint32_t config_addr,
                uint32_t result_addr,
                uint32_t result_size,
                const autorun_platform_ops_t *ops,
                autorun_context_t **ctx_out);

/**
 * @brief 初始化框架 (使用已映射的指针版本)
 *
 * 与 autorun_init() 功能相同，但接受直接指针而非物理地址。
 * 适用于已经完成内存映射的场景 (如Linux用户态程序)。
 *
 * @param[in]  config      配置数据指针 (已映射到虚拟地址空间)
 * @param[in]  result      结果区域指针
 * @param[in]  result_size 结果区域大小
 * @param[in]  ops         平台操作函数表
 * @param[out] ctx_out     上下文指针
 * @return int 错误码 (0=成功)
 */
int autorun_init_mapped(const autorun_config_t *config,
                       autorun_result_t *result,
                       uint32_t result_size,
                       const autorun_platform_ops_t *ops,
                       autorun_context_t **ctx_out);

/**
 * @brief 设置运行选项
 *
 * 在初始化后、执行前调用，自定义框架行为。
 *
 * @param[in] ctx  框架上下文 (由autorun_init返回)
 * @param[in] opts 选项配置 (NULL则使用默认值)
 * @return int 0=成功
 */
int autorun_set_options(autorun_context_t *ctx,
                        const autorun_options_t *opts);

/**
 * @brief 注册回调函数
 *
 * 用于监控执行进度、干预决策等高级功能。
 * 所有回调参数均可为NULL (表示不启用该回调)。
 *
 * @param[in] ctx             框架上下文
 * @param[in] on_task_start    任务开始回调 (可为NULL)
 * @param[in] on_task_complete 任务完成回调 (可为NULL)
 * @param[in] on_stage_start   阶段开始回调 (可为NULL)
 * @param[in] on_stage_complete阶段完成回调 (可为NULL)
 * @return int 0=成功
 */
int autorun_set_callbacks(autorun_context_t *ctx,
                          autorun_task_start_cb_t on_task_start,
                          autorun_task_complete_cb_t on_task_complete,
                          autorun_stage_start_cb_t on_stage_start,
                          autorun_stage_complete_cb_t on_stage_complete);

/**
 * @brief 执行指定阶段的所有任务
 *
 * 这是框架的核心执行函数。按照配置中任务的顺序依次执行，
 * 处理重试、超时、错误等情况，并将结果写入结果区域。
 *
 * 典型用法:
 * @code
 *   // BL1阶段
 *   ret = autorun_run_stage(ctx, "bl1");
 *   if (ret != 0 && is_critical_failure(ctx)) {
 *       // 处理关键失败
 *   }
 *   
 *   // 跳转到BL31...
 *   // BL31阶段
 *   ret = autorun_run_stage(ctx, "bl31");
 *   // ... 其他阶段
 * @endcode
 *
 * @param[in] ctx        框架上下文
 * @param[in] stage_name 要执行的阶段名称 ("bl1"/"bl31"/"rtthread"/"uboot"/"linux")
 *
 * @return int
 *         0 = 阶段全部通过
 *         AUTORUN_ERR_STAGE_NOT_FOUND = 未找到指定阶段
 *         AUTORUN_ERR_CRITICAL_FAIL = 关键任务失败
 *         AUTORUN_ERR_TIMEOUT = 全局超时
 *         AUTORUN_ERR_EXEC_ERROR = 命令执行错误
 *
 * @note 执行流程:
 *       1. 查找阶段配置 (by stage_name)
 *       2. 检查阶段是否启用
 *       3. 触发 on_stage_start 回调
 *       4. 遍历该阶段的所有任务:
 *          a. 触发 on_task_start 回调
 *          b. 调用 task_executor 执行任务
 *          c. 检查期望结果
 *          d. 失败则根据 retry_count 重试
 *          e. 触发 on_task_complete 回调
 *          f. 关键任务失败则中止
 *       5. 触发 on_stage_complete 回调
 *       6. 更新统计信息和结果区域
 */
int autorun_run_stage(autorun_context_t *ctx,
                     const char *stage_name);

/**
 * @brief 执行所有启用的阶段
 *
 * 按照execution_order顺序依次执行所有启用的阶段。
 * 适用于需要一次性跑完全部场景的情况。
 *
 * @param[in] ctx 框架上下文
 * @return int 0=全部通过, 非0=有失败
 */
int autorun_run_all(autorun_context_t *ctx);

/**
 * @brief 暂停执行 (预留)
 *
 * @param[in] ctx 框架上下文
 * @return int 0=成功
 */
int autorun_pause(autorun_context_t *ctx);

/**
 * @brief 恢复执行 (预留)
 *
 * @param[in] ctx 框架上下文
 * @return int 0=成功
 */
int autorun_resume(autorun_context_t *ctx);

/**
 * @brief 中止执行
 *
 * 立即停止当前任务和阶段的执行。
 * 已完成的任务结果保留，当前任务标记为SKIPPED或ERROR。
 *
 * @param[in] ctx 框架上下文
 * @return int 0=成功
 */
int autorun_abort(autorun_context_t *ctx);

/* ================================================================== */
/*                      状态查询API                                   */
/* ================================================================== */

/**
 * @brief 获取框架当前状态
 *
 * @param[in] ctx 框架上下文
 * @return autorun_state_t 当前状态
 */
autorun_state_t autorun_get_state(const autorun_context_t *ctx);

/**
 * @brief 获取执行结果
 *
 * 返回指向结果区域的指针，可直接访问详细的执行结果。
 *
 * @param[in] ctx 框架上下文
 * @return const autorun_result_t* 结果指针 (可能为NULL如果未初始化)
 */
const autorun_result_t* autorun_get_result(const autorun_context_t *ctx);

/**
 * @brief 获取统计信息
 *
 * @param[in]  ctx  框架上下文
 * @param[out] stats 统计信息输出 (不能为NULL)
 * @return int 0=成功
 */
int autorun_get_statistics(const autorun_context_t *ctx,
                           autorun_statistics_t *stats);

/**
 * @brief 获取指定任务的执行结果
 *
 * @param[in]  ctx     框架上下文
 * @param[in]  task_id 任务ID
 * @return const task_result_t* 任务结果指针 (未找到返回NULL)
 */
const task_result_t* autorun_get_task_result(const autorun_context_t *ctx,
                                            uint32_t task_id);

/**
 * @brief 检查是否有失败任务
 *
 * @param[in] ctx 框架上下文
 * @return true=有失败, false=全部通过
 */
bool autorun_has_failures(const autorun_context_t *ctx);

/**
 * @brief 检查是否有关键任务失败
 *
 * @param[in] ctx 框架上下文
 * @return true=有关键任务失败
 */
bool autorun_has_critical_failure(const autorun_context_t *ctx);

/* ================================================================== */
/*                      工具函数                                       */
/* ================================================================== */

/**
 * @brief 验证配置数据有效性
 *
 * 公开版本的配置验证函数，可在初始化前单独调用。
 *
 * @param[in] config 配置数据指针
 * @return int 0=有效, 错误码见 autorun_config.h
 */
int autorun_validate_config(const autorun_config_t *config);

/**
 * @brief 获取配置结构的实际大小
 *
 * 考虑到变长数组，计算实际的内存占用。
 *
 * @param[in] config 配置数据指针
 * @return uint32_t 实际大小 (bytes), 0=无效
 */
uint32_t autorun_get_config_size(const autorun_config_t *config);

/**
 * @brief 获取结果结构的实际大小
 *
 * @param[in] result 结果数据指针
 * @return uint32_t 实际大小 (bytes)
 */
uint32_t autorun_get_result_size(const autorun_result_t *result);

/**
 * @brief 查找阶段配置
 *
 * @param[in] config     配置数据
 * @param[in] stage_name 阶段名称
 * @return const stage_config_t* 阶段配置指针 (未找到返回NULL)
 */
const stage_config_t* autorun_find_stage(
    const autorun_config_t *config,
    const char *stage_name
);

/**
 * @brief 查找任务配置
 *
 * @param[in] config 配置数据
 * @param[in] task_id 任务ID
 * @return const task_config_t* 任务配置指针 (未找到返回NULL)
 */
const task_config_t* autorun_find_task(
    const autorun_config_t *config,
    uint32_t task_id
);

/**
 * @brief 将状态枚举转换为可读字符串
 *
 * @param[in] state 状态枚举值
 * @return const char* 状态描述字符串
 */
const char* autorun_state_string(autorun_state_t state);

/**
 * @brief 将任务状态枚举转换为可读字符串
 *
 * @param[in] status 任务状态枚举值
 * @return const char* 状态描述字符串
 */
const char* autorun_task_status_string(task_status_t status);

/* ================================================================== */
/*                      清理与销毁                                     */
/* ================================================================== */

/**
 * @brief 清理框架资源
 *
 * 释放内部分配的资源（如果有），重置状态。
 * 调用后上下文不可再使用，除非重新初始化。
 *
 * @param[in] ctx 框架上下文
 * @return int 0=成功
 *
 * @note 此函数不会释放传入的config/result/ops资源，
 *       这些由调用者负责管理。
 */
int autorun_cleanup(autorun_context_t *ctx);

/**
 * @brief 重置框架状态 (保留配置)
 *
 * 清除执行结果和统计信息，但保留配置和平台接口。
 * 可用于重新执行相同的配置。
 *
 * @param[in] ctx 框架上下文
 * @return int 0=成功
 */
int autorun_reset(autorun_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AUTORUN_CORE_H */

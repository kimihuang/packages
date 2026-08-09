/**
 * @file autorun_config.h
 * @brief 启动各阶段自动运行框架 - 配置数据结构定义
 *
 * 本文件定义了自动运行框架的核心数据结构，包括：
 * - 全局配置 (autorun_config_t)
 * - 阶段配置 (stage_config_t)
 * - 任务配置 (task_config_t)
 * - 期望结果 (expected_result_t)
 * - 执行结果 (autorun_result_t, task_result_t)
 *
 * 设计原则：
 * 1. 结构体紧凑：使用 packed 属性消除填充字节，减少内存占用
 * 2. 定长类型：所有字段使用定长整数类型，确保跨平台二进制兼容
 * 3. 固定长度：使用固定长度数组，避免动态内存分配（适配BL1等资源受限环境）
 * 4. 版本控制：魔数+版本号机制，支持向前/向后兼容
 * 5. 完整性保护：CRC32校验确保配置数据未被篡改
 *
 * 内存布局：
 * ┌──────────────────────────────────────┐
 * │         Header (16 bytes)             │ ← magic + version + size + crc
 * ├──────────────────────────────────────┤
 │       Global Config (~24 bytes)        │ ← log_level, timeout, result addr...
 * ├──────────────────────────────────────┤
 │       Stage Count (4 bytes)            │
 * ├──────────────────────────────────────┤
 │       Stages[] (变长)                  │ ← 每个阶段包含多个任务
 │  ┌─────────────────────────────────┐   │
 │  │ Stage Header (28 bytes)          │   │
 │  │ Tasks[0..N] (每任务 ~320 bytes)  │   │
 │  └─────────────────────────────────┘   │
 * └──────────────────────────────────────┘
 *
 * 典型大小估算：
 * - 最小配置（1阶段1任务）：~400 bytes
 * - 典型配置（5阶段20任务）：~8-12 KB
 * - 最大配置（5阶段80任务）：~26 KB
 *
 * @note 本头文件可被以下模块引用：
 *       - PC侧工具链 (config_generator.py): JSON → Binary 转换
 *       - Target侧核心引擎 (autorun_core.c): 解析和执行配置
 *       - 各平台适配层 (*_port.c): 读取配置并执行任务
 *
 * @warning 修改本文件中的任何结构体定义时，必须同步更新：
 *           1. PC侧 config_generator.py 的转换逻辑
 *           2. AUTORUN_VERSION_* 版本号（破坏性变更需升级主版本号）
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef AUTORUN_CONFIG_H
#define AUTORUN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*                         头文件依赖                                */
/* ================================================================== */

#include <stdint.h>
#include <stdbool.h>

/* ================================================================== */
/*                    平台相关宏定义                                 */
/* ================================================================== */

/**
 * @brief 编译器属性宏：结构体紧凑排列（无填充字节）
 *
 * 不同编译器的 packed 属性语法：
 * - GCC/Clang: __attribute__((packed))
 * - MSVC: #pragma pack(1)
 * - IAR: __packed
 */
#if defined(__GNUC__) || defined(__clang__)
    #define AUTORUN_PACKED __attribute__((packed))
#elif defined(_MSC_VER)
    #define AUTORUN_PACKED
    /* MSVC需要在结构体前加 #pragma pack(push, 1)，后加 #pragma pack(pop) */
#else
    #define AUTORUN_PACKED
#endif

/**
 * @brief 编译器属性宏：对齐方式
 */
#if defined(__GNUC__) || defined(__clang__)
    #define AUTORUN_ALIGNED(n) __attribute__((aligned(n)))
#else
    #define AUTORUN_ALIGNED(n) __declspec(align(n))
#endif

/* ================================================================== */
/*                      版本与魔数定义                               */
/* ================================================================== */

/**
 * @brief 配置文件魔数 (Magic Number)
 *
 * 用于快速识别有效的配置数据。在配置数据的起始位置检查此值，
 * 如果不匹配则说明：
 * - 该地址没有有效配置
 * - 配置数据损坏
 * - 地址错误
 *
 * 选择此值的原因：
 * - 0xA55A5AA5 具有明显的模式特征（A5交替）
 * - 不是全0或全F（避免与未初始化内存混淆）
 * - 不太可能是普通数据
 */
#define AUTORUN_MAGIC               0xA55A5AA5UL

/**
 * @brief 结果区域魔数
 *
 * 用于标识执行结果的起始位置。
 * 0xBEEFBEEF = "BEEF" (牛肉)，易于记忆和识别
 */
#define AUTORUN_RESULT_MAGIC        0xBEEFBEEFUL

/**
 * @brief 主版本号 (Major Version)
 *
 * 主版本号变更表示**不兼容的结构体修改**：
 * - 字段删除或重命名
 * - 字段类型改变（如 uint32_t → uint64_t）
 * - 结构体整体重新排序
 *
 * 主版本不同时，新版本的框架无法读取旧版本配置
 */
#define AUTORUN_VERSION_MAJOR       1

/**
 * @brief 次版本号 (Minor Version)
 *
 * 次版本号变更表示**向后兼容的扩展**：
 * - 新增可选字段（在结构体末尾）
 * - 新增枚举值
 * - 增大常量限制（如 TASK_NAME_MAX_LEN）
 *
 * 次版本增加时，旧版框架可以读取新版配置（忽略未知字段）
 */
#define AUTORUN_VERSION_MINOR       0

/**
 * @brief 补丁版本号 (Patch Version)
 *
 * 补丁版本变更表示**bug修复**，不影响数据结构
 */
#define AUTORUN_VERSION_PATCH       0

/**
 * @brief 将版本号组合为单个整数，便于比较
 *
 * 格式: MMMMNNNNPPPP (各占12位)
 * 示例: v1.2.3 → 0x000100200003
 */
#define AUTORUN_VERSION_COMBINED    ((AUTORUN_VERSION_MAJOR << 20) | \
                                     (AUTORUN_VERSION_MINOR << 10) | \
                                      AUTORUN_VERSION_PATCH)

/* ================================================================== */
/*                     长度限制常量                                  */
/* ================================================================== */

/*
 * 以下常量定义了各种字符串字段的最大长度。
 * 设计时需要权衡：
 * - 太小：可能不够用，导致截断
 * - 太大：浪费内存（尤其BL1阶段的SRAM空间宝贵）
 *
 * 当前值基于实际使用场景的经验值设定。
 */

/** @brief 任务名称最大长度（含终止符）*/
#define AUTORUN_TASK_NAME_MAX_LEN       32

/** @brief Shell命令字符串最大长度（含终止符）*/
#define AUTORUN_CMD_MAX_LEN             256

/** @brief 匹配模式字符串最大长度（含终止符）
 *         用于 string_match 和 regex 类型的 pattern 字段
 */
#define AUTORUN_PATTERN_MAX_LEN         128

/** @brief 阶段名称最大长度（含终止符）*/
#define AUTORUN_STAGE_NAME_MAX_LEN      16

/** @brief 输出摘要最大长度（含终止符）
 *         用于记录任务执行输出的前 N 个字符
 */
#define AUTORUN_OUTPUT_SUMMARY_MAX_LEN  256

/** @brief JSON key 名称最大长度（含终止符）*/
#define AUTORUN_JSON_KEY_MAX_LEN        48

/** @brief 环境变量名最大长度（含终止符）*/
#define AUTORUN_ENV_KEY_MAX_LEN         64

/** @brief 环境变量值最大长度（含终止符）*/
#define AUTORUN_ENV_VAL_MAX_LEN         256

/** @brief 单个阶段支持的最大任务数量
 *
 * 限制原因：
 * - BL1阶段SRAM有限，不能太多任务
 * - 结果缓冲区大小受限
 * - 实际测试中单阶段超过16个任务的情况很少
 */
#define AUTORUN_MAX_TASKS_PER_STAGE     16

/** @brief 最大支持的启动阶段数量
 *
 * 当前支持的阶段：BL1, BL31, RT-Thread, U-Boot, Linux
 * 预留额外空间以备未来扩展（如SPL、OPTEE等）
 */
#define AUTORUN_MAX_STAGES              8

/** @brief 每个任务最多支持的期望结果检查项数量
 *         （用于 json_output 类型中的多key检查）
 */
#define AUTORUN_MAX_CHECK_KEYS          8

/** @brief 每个任务最多支持的环境变量数量（仅Linux阶段使用）*/
#define AUTORUN_MAX_ENV_VARS            8

/** @brief 每个 stage 最多支持的 pre/post commands 数量 */
#define AUTORUN_MAX_COMMANDS            8

/** @brief 命令字符串最大长度（pre/post command）*/
#define AUTORUN_COMMAND_STR_MAX_LEN     256

/* ================================================================== */
/*                        枚举类型定义                               */
/* ================================================================== */

/**
 * @brief 日志级别枚举
 *
 * 定义了框架的日志输出级别，数值越小输出越少。
 * 通过 global_config.log_level 控制输出详细程度。
 */
typedef enum AUTORUN_PACKED {
    LOG_LEVEL_OFF     = 0,    /**< 关闭所有日志输出 */
    LOG_LEVEL_ERROR   = 1,    /**< 仅输出错误信息 */
    LOG_LEVEL_WARN    = 2,    /**< 输出警告和错误 */
    LOG_LEVEL_INFO    = 3,    /**< 输出一般信息、警告、错误（推荐）*/
    LOG_LEVEL_DEBUG   = 4,    /**< 输出所有调试信息（开发调试用）*/
    LOG_LEVEL_VERBOSE = 5     /**< 极其详细的输出（性能分析用）*/
} log_level_t;

/**
 * @brief 期望结果类型枚举
 *
 * 定义了任务执行完成后如何判断是否成功。
 * 每种类型对应 expected_result_t 联合体的不同成员。
 */
typedef enum AUTORUN_PACKED {
    /**
     * @brief 返回码匹配
     *
     * 检查 shell 命令的退出码是否等于预期值。
     * 适用场景：大多数命令行程序
     * 示例：ls 返回 0 表示成功
     */
    EXPECT_TYPE_RETURN_CODE = 0,

    /**
     * @brief 子串匹配（精确包含）
     *
     * 检查命令输出中是否包含指定的字符串（区分大小写）。
     * 适用场景：简单的成功/失败标志
     * 示例：输出包含 "PASS" 或 "OK"
     */
    EXPECT_TYPE_STRING_MATCH = 1,

    /**
     * @brief 正则表达式匹配
     *
     * 使用正则表达式匹配命令输出。
     * 适用场景：复杂的输出格式、变化的数值
     * 示例：匹配 "speed=1234 Mbps" 中的数字
     *
     * @note BL1/BL31等轻量级环境可能不支持正则库，
     *       此时可降级为简单的通配符匹配
     */
    EXPECT_TYPE_REGEX = 2,

    /**
     * @brief 校验和验证
     *
     * 对命令输出（或指定文件）计算校验和并与预期值比较。
     * 支持算法：CRC32, MD5, SHA256
     * 适用场景：数据完整性验证
     * 示例：Flash读取后的数据校验
     */
    EXPECT_TYPE_CHECKSUM = 3,

    /**
     * @brief JSON输出解析
     *
     * 将命令输出的JSON字符串解析为对象，
     * 检查指定的key是否存在，并可验证其值。
     * 适用场景：结构化输出的现代CLI工具
     * 示例：fio --output-format=json 的输出解析
     *
     * @note 需要JSON解析库（如cJSON），BL1环境不支持
     */
    EXPECT_TYPE_JSON_OUTPUT = 4,

    /**
     * @brief 数值范围检查
     *
     * 将返回码或从输出中提取的数值与范围比较。
     * 适用场景：性能指标、测量值
     * 示例：网络速度应在 100-1000 Mbps 范围内
     */
    EXPECT_TYPE_RANGE = 5,

    /** @brief 保留：供未来扩展使用 */
    EXPECT_TYPE_RESERVED_6 = 6,
    EXPECT_TYPE_RESERVED_7 = 7
} expect_type_t;

/**
 * @brief 校验算法枚举
 *
 * 用于 EXPECT_TYPE_CHECKSUM 类型的算法选择。
 */
typedef enum AUTORUN_PACKED {
    CHECKSUM_ALG_CRC32  = 0,    /**< CRC32 (推荐，速度快) */
    CHECKSUM_ALG_MD5    = 1,    /**< MD5 (128位，通用) */
    CHECKSUM_ALG_SHA256 = 2,    /**< SHA-256 (256位，高安全性) */
    CHECKSUM_ALG_CRC16  = 3,    /**< CRC16 (轻量级，适用于小数据) */
    CHECKSUM_ALG_XOR    = 4     /**< XOR校验（最简单，但可靠性低）*/
} checksum_alg_t;

/**
 * @brief 任务执行状态枚举
 *
 * 记录任务的最终执行结果，写入结果区域。
 */
typedef enum AUTORUN_PACKED {
    /**
     * @brief 待执行
     *
     * 初始状态，任务尚未开始执行。
     * 也用于被跳过的阶段中的任务。
     */
    TASK_STATUS_PENDING  = 0,

    /**
     * @brief 执行中
     *
     * 任务正在执行（shell命令正在运行）。
     * 如果系统崩溃，任务将保持此状态。
     */
    TASK_STATUS_RUNNING  = 1,

    /**
     * @brief 成功
     *
     * 任务执行完成且符合预期结果。
     */
    TASK_STATUS_SUCCESS  = 2,

    /**
     * @brief 失败
     *
     * 任务执行完成但不符合预期（重试次数耗尽后仍失败）。
     */
    TASK_STATUS_FAILED   = 3,

    /**
     * @brief 超时
     *
     * 任务执行时间超过了 timeout_ms 限制。
     * 可能是命令死锁或系统挂起。
     */
    TASK_STATUS_TIMEOUT  = 4,

    /**
     * @brief 跳过
     *
     * 任务被显式禁用（enabled=false），
     * 或前置关键任务失败导致后续任务被跳过。
     */
    TASK_STATUS_SKIPPED  = 5,

    /**
     * @brief 错误
     *
     * 执行过程中发生异常错误（如命令不存在、权限不足等）。
     */
    TASK_STATUS_ERROR    = 6
} task_status_t;

/**
 * @brief 条件操作符枚举
 *
 * 用于 json_output 类型中的条件表达式。
 */
typedef enum AUTORUN_PACKED {
    COND_OP_EQUAL        = 0,    /**< 等于 (==) */
    COND_OP_NOT_EQUAL    = 1,    /**< 不等于 (!=) */
    COND_OP_GREATER      = 2,    /**< 大于 (>) */
    COND_OP_GREATER_EQ   = 3,    /**< 大于等于 (>=) */
    COND_OP_LESS         = 4,    /**< 小于 (<) */
    COND_OP_LESS_EQ      = 5,    /**< 小于等于 (<=) */
    COND_OP_CONTAINS     = 6,    /**< 包含 (字符串) */
    COND_OP_START_WITH   = 7,    /**< 以...开头 */
    COND_OP_END_WITH     = 8,    /**< 以...结尾 */
    COND_OP_REGEX_MATCH  = 9     /**< 正则匹配 */
} condition_op_t;

/* ================================================================== */
/*                   核心数据结构定义                                */
/* ================================================================== */

/**
 * @struct expected_result_t
 * @brief 期望结果定义（联合体结构）
 *
 * 定义了任务执行成功与否的判定条件。
 * 使用联合体（union）节省内存，因为一次只使用一种判定方式。
 *
 * 内存占用：最大的成员决定整个联合体的大小
 * - return_code: 4 bytes
 * - string_match: PATTERN_MAX_LEN bytes (~128)
 * - regex: PATTERN_MAX_LEN bytes (~128)
 * - checksum: 8 bytes
 * - json_output: ~440 bytes (最大)
 * - range: 8 bytes
 *
 * 总大小约: 440 bytes (由 json_output 决定)
 */
typedef struct AUTORUN_PACKED {
    /** @brief 结果类型（决定使用联合体的哪个成员）*/
    expect_type_t type;

    /**
     * @brief 结果判定条件（联合体）
     *
     * 根据 type 字段的值选择对应的成员：
     * - type == RETURN_CODE → 使用 return_code
     * - type == STRING_MATCH → 使用 string_match
     * - type == REGEX → 使用 regex
     * - type == CHECKSUM → 使用 checksum
     * - type == JSON_OUTPUT → 使用 json_output
     * - type == RANGE → 使用 range
     */
    union AUTORUN_PACKED {
        /**
         * @brief 返回码匹配
         *
         * 检查: actual_return_code == expected_code
         */
        struct AUTORUN_PACKED {
            int32_t expected_code;    /**< 期望的返回码 (通常0=成功)*/
        } return_code;

        /**
         * @brief 子串匹配
         *
         * 检查: output.contains(pattern)
         * 支持简单的通配符: * (任意字符), ? (单个字符)
         */
        struct AUTORUN_PACKED {
            char pattern[AUTORUN_PATTERN_MAX_LEN];  /**< 要匹配的子串*/
        } string_match;

        /**
         * @brief 正则表达式匹配
         *
         * 检查: output.matches(regex_pattern)
         * 支持 POSIX 扩展正则语法
         *
         * @note 在资源受限环境(BL1)中可能退化为简单匹配
         */
        struct AUTORUN_PACKED {
            char pattern[AUTORUN_PATTERN_MAX_LEN];  /**< 正则表达式*/
        } regex;

        /**
         * @brief 校验和验证
         *
         * 检查: checksum(data, algorithm) == expected_value
         * 数据来源可以是命令输出或指定文件路径
         */
        struct AUTORUN_PACKED {
            checksum_alg_t algorithm;    /**< 校验算法*/
            uint32_t expected_value;      /**< 期望的校验值*/
            char data_source[64];         /**< 数据源: "stdout" 或文件路径*/
        } checksum;

        /**
         * @brief JSON输出解析
         *
         * 解析命令输出的JSON，检查指定的key是否存在及值是否符合条件。
         * 支持嵌套key（用点号分隔）：如 "jobs[0].write.iops"
         */
        struct AUTORUN_PACKED {
            /** @brief 要检查的key名称列表*/
            char check_keys[AUTORUN_MAX_CHECK_KEYS][AUTORUN_JSON_KEY_MAX_LEN];

            /** @brief key的数量 (<= MAX_CHECK_KEYS)*/
            uint8_t key_count;

            /**
             * @brief 值条件检查 (可选)
             *
             * 如果不需要检查具体值，可将 value_condition.enabled 设为 false
             */
            struct AUTORUN_PACKED {
                char key_name[AUTORUN_JSON_KEY_MAX_LEN];  /**< 要检查值的key*/
                condition_op_t op;                          /**< 比较操作符*/

                /**
                 * @brief 期望值（字符串形式）
                 *
                 * 框架会尝试将JSON值转换为字符串后比较，
                 * 或根据操作符进行数值比较
                 */
                char expected_value[64];
                bool enabled;                               /**< 是否启用此条件*/
            } value_conditions[AUTORUN_MAX_CHECK_KEYS];
        } json_output;

        /**
         * @brief 数值范围检查
         *
         * 检查: min_val <= actual_value <= max_val
         * actual_value 可以是返回码或从输出提取的数值
         */
        struct AUTORUN_PACKED {
            int32_t min_val;    /**< 最小值 (含)*/
            int32_t max_val;    /**< 最大值 (含)*/
            char extract_regex[64];  /**< 从输出提取数值的正则(可选,空则用返回码)*/
        } range;
    };
} expected_result_t;

/**
 * @struct env_var_t
 * @brief 环境变量定义（仅Linux阶段使用）
 *
 * 用于设置任务执行时的环境变量。
 * 例如: PATH=/usr/bin:/bin, LD_LIBRARY_PATH=/usr/lib
 */
typedef struct AUTORUN_PACKED {
    char key[AUTORUN_ENV_KEY_MAX_LEN];   /**< 变量名 (如 "PATH")*/
    char val[AUTORUN_ENV_VAL_MAX_LEN];   /**< 变量值 (如 "/usr/bin:/bin")*/
} env_var_t;

/**
 * @struct task_config_t
 * @brief 单个任务配置结构体
 *
 * 定义了一个自动化测试任务的完整描述。
 * 这是框架中最核心的数据结构之一。
 *
 * 内存占用估算:
 * - 固定字段: ~20 bytes
 * - name: 32 bytes
 * - command: 256 bytes
 * - expected_result: ~440 bytes (最大情况)
 * - env_vars: ~2.5 KB (最大情况)
 * - 总计: ~3.2 KB (最坏情况)
 * - 典型情况: ~500-800 bytes
 */
typedef struct AUTORUN_PACKED {
    /* ===== 基本信息 ===== */

    /** @brief 任务全局唯一ID
     *
     * ID分配规则（每个阶段固定ID范围）:
     *
     * ┌─────────────┬──────────────┬──────────────┐
     * │   阶段      │   ID 范围     │   ID 数量     │
     * ├─────────────┼──────────────┼──────────────┤
     * │ BL1         │ 0x00000000 - 0x000003FF │ 1,000      │
     * │             │ (0        - 999)       │            │
     * ├─────────────┼──────────────┼──────────────┤
     * │ BL31        │ 0x000003E8 - 0x000007CF │ 1,000      │
     * │             │ (1000     - 1999)       │            │
     * ├─────────────┼──────────────┼──────────────┤
     * │ RT-Thread   │ 0x000007D0 - 0x00000BA7 │ 1,000      │
     * │             │ (2000     - 2999)       │            │
     * ├─────────────┼──────────────┼──────────────┤
     * │ U-Boot      │ 0x00000BB8 - 0x00000F9F │ 1,000      │
     * │             │ (3000     - 3999)       │            │
     * ├─────────────┼──────────────┼──────────────┤
     * │ Linux       │ 0x00000FA0 - 0x0000368F │ 10,000     │
     * │             │ (4000     - 13999)      │            │
     * ├─────────────┼──────────────┼──────────────┤
     * │ 自定义/扩展  │ 0x00003690 - 0xFFFFFFFF │ ~4.29亿    │
     * │             │ (14000    + )           │            │
     * └─────────────┴──────────────┴──────────────┘
     *
     * ID编号建议:
     * - 每个阶段内从基数开始顺序递增
     * - 示例: BL1任务从0开始(0,1,2,...), BL31任务从1000开始(1000,1001,...)
     * - 预留空间便于后续插入新任务而不需重排ID
     *
     * 用于在结果中唯一标识任务，也用于任务间依赖关系
     */
    uint32_t id;

    /** @brief 任务人类可读名称
     *
     * 用于日志输出和结果显示。
     * 应简洁明了地描述任务用途。
     * 示例: "ddr_init_test", "network_ping"
     */
    char name[AUTORUN_TASK_NAME_MAX_LEN];

    /** @brief 要执行的Shell命令
     *
     * 命令格式取决于当前阶段的Shell能力:
     * - BL1/BL31: 预注册的简化命令
     * - RT-Thread: finsh/msh 命令
     * - U-Boot: 完整U-Boot Shell命令
     * - Linux: Bash/Sh 命令（支持管道、重定向等）
     *
     * 特殊语法:
     * - "&&" : 顺序执行（前一个成功才执行下一个）
     * - "||" : 或逻辑（前一个失败才执行下一个）
     * - ";"  : 无条件顺序执行
     */
    char command[AUTORUN_CMD_MAX_LEN];

    /* ===== 执行控制 ===== */

    /** @brief 是否启用此任务
     *
     * false 表示跳过此任务（状态记为 SKIPPED）
     * 可用于临时禁用某些任务而不删除配置
     */
    bool enabled;

    /** @brief 是否为关键任务
     *
     * true 表示如果此任务失败，则：
     * - 终止当前阶段的后续任务
     * - 将阶段结果标记为失败
     * - 如果 continue_on_error=false，终止整个流程
     *
     * 典型关键任务: DDR初始化、核心外设检测
     * 非关键任务: 性能测试、可选功能验证
     */
    bool critical;

    /** @brief 任务执行超时时间（毫秒）
     *
     * 超过此时间未完成则视为 TIMEOUT 失败。
     * 设置建议:
     * - 简单命令: 1-5秒
     * - 外设操作: 5-30秒
     * - 网络/存储测试: 30-120秒
     * - 压力测试: 120-600秒
     *
     * 特殊值: 0 表示无限等待（不推荐，可能导致死锁）
     */
    uint32_t timeout_ms;

    /** @brief 失败后的重试次数
     *
     * 任务首次失败后，最多重试的次数。
     * 重试期间任务状态仍为 RUNNING。
     *
     * 建议:
     * - 稳定的命令: 0 (不重试)
     * - 可能偶发的命令: 1-2
     * - 网络相关: 2-3
     */
    uint8_t retry_count;

    /** @brief 重试间隔时间（毫秒）
     *
     * 每次重试前的等待时间。
     * 给系统恢复的时间（如释放资源、网络稳定等）
     */
    uint32_t retry_delay_ms;

    /* ===== 结果判定 ===== */

    /** @brief 期望结果定义
     *
     * 定义了如何判断任务执行是否成功。
     * 详见 expected_result_t 说明
     */
    expected_result_t expected_result;

    /* ===== 日志与输出 ===== */

    /** @brief 是否记录命令输出到日志
     *
     * true 表示将命令的标准输出和标准错误保存到日志。
     * 便于事后分析和调试。
     *
     * 注意: 会增加内存和存储开销
     */
    bool log_output;

    /** @brief 保留字段（对齐和未来扩展）*/
    uint8_t reserved[3];

    /* ===== 环境变量（仅Linux阶段有效）===== */

    /** @brief 环境变量数量*/
    uint8_t env_count;

    /** @brief 环境变量列表*/
    env_var_t env_vars[AUTORUN_MAX_ENV_VARS];

} task_config_t;

/**
 * @struct stage_config_t
 * @brief 单个启动阶段配置结构体
 *
 * 定义了一个启动阶段（如BL1、Linux）的所有任务和控制参数。
 *
 * 内存占用估算:
 * - 固定字段: ~28 bytes
 * - tasks[16]: ~51 KB (最坏情况，每个任务3.2KB)
 * - pre_commands + post_commands: ~4 KB
 * - 总计: ~55 KB (最坏情况)
 * - 典型情况: ~5-15 KB
 */
typedef struct AUTORUN_PACKED {
    /* ===== 阶段标识 ===== */

    /** @brief 阶段名称
     *
     * 预定义值: "bl1", "bl31", "rtthread", "uboot", "linux"
     * 框架通过此名称查找当前阶段的配置
     */
    char stage_name[AUTORUN_STAGE_NAME_MAX_LEN];

    /** @brief 是否启用此阶段
     *
     * false 表示完全跳过此阶段（所有任务都不执行）
     */
    bool enabled;

    /** @brief 阶段执行顺序
     *
     * 多阶段执行时的顺序编号（从1开始）。
     * 框架应按 execution_order 升序执行各阶段。
     *
     * 标准顺序:
     * 1. bl1
     * 2. bl31
     * 3. rtthread (或 uboot，取决于启动流程)
     * 4. uboot (或 rtthread)
     * 5. linux
     */
    uint8_t execution_order;

    /* ===== 预/后置命令（仅Linux/U-Boot等丰富环境支持）===== */

    /** @brief 阶段开始前执行的准备命令数量*/
    uint8_t pre_command_count;

    /** @brief 阶段结束后执行的清理命令数量*/
    uint8_t post_command_count;

    /** @brief 准备命令列表
     *
     * 在阶段第一个任务执行前依次执行这些命令。
     * 典型用途:
     * - 挂载文件系统
     * - 创建临时目录
     * - 清空旧日志
     * - 加载内核模块
     */
    char pre_commands[AUTORUN_MAX_COMMANDS][AUTORUN_COMMAND_STR_MAX_LEN];

    /** @brief 清理命令列表
     *
     * 在阶段所有任务执行完后依次执行这些命令。
     * 典型用途:
     * - 同步文件系统缓存 (sync)
     * - 收集日志
     * - 卸载文件系统
     * - 释放资源
     */
    char post_commands[AUTORUN_MAX_COMMANDS][AUTORUN_COMMAND_STR_MAX_LEN];

    /* ===== 任务列表 ===== */

    /** @brief 此阶段的任务数量
     *
     * 必须 <= AUTORUN_MAX_TASKS_PER_STAGE
     */
    uint8_t task_count;

    /** @brief 保留字段*/
    uint8_t reserved[2];

    /** @brief 任务配置数组
     *
     * 按执行顺序排列的任务列表。
     * 框架从 tasks[0] 开始依次执行到 tasks[task_count-1]
     */
    task_config_t tasks[AUTORUN_MAX_TASKS_PER_STAGE];

} stage_config_t;

/**
 * @struct autorun_global_config_t
 * @brief 全局配置结构体
 *
 * 定义了影响所有阶段的全局控制参数。
 * 这些参数位于配置数据的头部区域之后。
 */
typedef struct AUTORUN_PACKED {
    /* ===== 日志控制 ===== */

    /** @brief 日志输出级别
     *
     * 控制 framework 的日志详细程度。
     * 详见 log_level_t 枚举
     *
     * 建议:
     * - 生产环境: LOG_LEVEL_INFO (3)
     * - 调试环境: LOG_LEVEL_DEBUG (4)
     */
    uint8_t log_level;

    /* ===== 错误处理策略 ===== */

    /** @brief 非关键任务失败时是否继续执行
     *
     * true: 记录失败但继续执行后续任务
     * false: 第一个非关键任务失败即停止阶段
     *
     * 注意: 此选项不影响关键任务(critical=true)的行为，
     *       关键任务失败总是会导致停止
     */
    bool continue_on_error;

    /* ===== 超时控制 ===== */

    /** @brief 全局超时时间（秒）
     *
     * 整个阶段（或全部流程）的最大允许执行时间。
     * 超过此时间强制终止。
     *
     * 特殊值: 0 表示无全局超时（仅靠各任务的timeout_ms控制）
     *
     * 建议: 根据最长测试场景设置，留有20%余量
     */
    uint16_t global_timeout_s;

    /* ===== 结果输出区域 ===== */

    /** @brief 结果输出内存地址（物理地址）
     *
     * 框架将执行结果写入此地址。
     * 必须是有效的可写内存区域（DDR/SRAM）。
     *
     * 各阶段的建议位置:
     * - BL1: SRAM顶部 (需确保不与其他数据冲突)
     * - BL31: Secure DDR 或共享SRAM
     * - RT-Thread/U-Boot/Linux: DDR保留区域
     *
     * PC侧工具可通过JTAG/SWD读取此地址获取结果
     */
    uint32_t result_output_addr;

    /** @brief 结果区域最大大小（字节）
     *
     * 框架写入结果时不会超过此大小。
     * 应根据最大任务数合理设置（见下方的估算公式）
     *
     * 估算公式:
     * sizeof(autorun_result_t) + max_tasks * sizeof(task_result_t)
     * ≈ 32 + max_tasks * 300
     *
     * 建议:
     * - 少量任务 (<10): 4KB
     * - 中等任务 (10-30): 8-16KB
     * - 大量任务 (>30): 32-64KB
     */
    uint32_t result_max_size;

    /* ===== 完成行为 ===== */

    /** @brief 全部任务完成后是否停机
     *
     * true: 进入死循环或低功耗状态，便于PC侧读取结果
     * false: 继续正常启动流程
     *
     * 适用场景:
     * - 自动化测试产线: true (需要可靠地读取结果)
     * - 开发调试: false (可能需要交互式Shell)
     */
    bool halt_on_completion;

    /** @brief 保留字段（对齐到4字节）*/
    uint8_t reserved[5];

} autorun_global_config_t;

/**
 * @struct autorun_config_header_t
 * @brief 配置数据头部结构
 *
 * 位于配置数据的最开始，用于快速校验和识别。
 * 所有字段都是固定的4字节宽度，便于解析。
 */
typedef struct AUTORUN_PACKED {
    /** @brief 魔数 (必须是 AUTORUN_MAGIC)*/
    uint32_t magic;

    /** @brief 主版本号 (必须是 AUTORUN_VERSION_MAJOR)*/
    uint16_t version_major;

    /** @brief 次版本号 (必须是 AUTORUN_VERSION_MINOR)*/
    uint16_t version_minor;

    /** @brief 整个配置结构的总大小（字节）
     *
     * 包括头部、全局配置、所有阶段和任务。
     * 用于:
     * 1. PC侧烧录时知道要写多少数据
     * 2. CRC32校验的范围
     * 3. 查找是否有后续配置（如多镜像场景）
     */
    uint32_t config_size;

    /** @brief 配置数据的CRC32校验值
     *
     * 计算范围: 从 magic 到最后一个字节（不包括此字段本身）
     * 算法: standard CRC32 (IEEE 802.3 / ISO 3309)
     * 多项式: 0x04C11DB7
     * 初始值: 0xFFFFFFFF
     *
     * 校验流程:
     * 1. PC侧生成配置时计算CRC并填入此字段
     * 2. Target侧读取时重新计算并比较
     * 3. 不匹配则拒绝执行（防止配置损坏或篡改）
     */
    uint32_t crc32;

} autorun_config_header_t;

/**
 * @struct autorun_config_t
 * @brief 完整配置数据结构（顶层）
 *
 * 这是配置数据的根结构体，包含了所有配置信息。
 * PC侧工具生成的二进制文件就是此结构体的序列化。
 *
 * 内存布局:
 * ┌─────────────────────────────────┐
 * │ header (20 bytes)               │  offset 0x00
 * ├─────────────────────────────────┤
 * │ global_config (~24 bytes)       │  offset 0x14
 * ├─────────────────────────────────┤
 * │ stage_count (4 bytes)           │  offset 0x2C
 * ├─────────────────────────────────┤
 * │ stages[0]                       │  offset 0x30
 * │   ├── stage_header (~28 bytes)  │
 * │   ├── pre/post commands         │
 * │   └── tasks[N]                  │
 * ├─────────────────────────────────┤
 * │ stages[1]                       │
 * │   ...                           │
 * ├─────────────────────────────────┤
 * │ ...                             │
 * ├─────────────────────────────────┤
 * │ stages[MAX_STAGES-1]            │
 * └─────────────────────────────────┘
 *
 * 总大小: 由 header.config_size 指示
 */
typedef struct AUTORUN_PACKED {
    /* ===== 头部信息 ===== */

    /** @brief 配置头部（魔数、版本、大小、CRC）*/
    autorun_config_header_t header;

    /* ===== 全局配置 ===== */

    /** @brief 全局控制参数*/
    autorun_global_config_t global_config;

    /* ===== 阶段配置 ===== */

    /** @brief 实际启用的阶段数量
     *
     * 必须 <= AUTORUN_MAX_STAGES
     * 注意: 这是"启用"的阶段数，不是stages数组中非空元素数
     */
    uint32_t stage_count;

    /** @brief 各阶段配置数组
     *
     * 按execution_order排序（建议但不强制）。
     * 框架遍历此数组，查找stage_name匹配当前阶段的条目。
     */
    stage_config_t stages[AUTORUN_MAX_STAGES];

} autorun_config_t;

/* ================================================================== */
/*                   执行结果数据结构                                */
/* ================================================================== */

/**
 * @struct task_result_t
 * @brief 单个任务执行结果
 *
 * 记录一个任务的执行过程和最终结果。
 * 由框架在运行时写入结果区域。
 */
typedef struct AUTORUN_PACKED {
    /* ===== 标识信息 ===== */

    /** @brief 对应的任务ID (来自 task_config_t.id)*/
    uint32_t task_id;

    /* ===== 执行状态 ===== */

    /** @brief 最终执行状态
     *
     * 可能的值:
     * - SUCCESS: 任务成功完成
     * - FAILED: 任务完成但不符合预期
     * - TIMEOUT: 执行超时
     * - SKIPPED: 任务被禁用或因前置失败而跳过
     * - ERROR: 执行过程中发生异常
     */
    task_status_t status;

    /* ===== 返回信息 ===== */

    /** @brief Shell命令的返回码
     *
     * 通常0表示成功，非0表示错误。
     * 具体含义取决于执行的命令。
     */
    int32_t return_code;

    /** @brief 实际执行耗时（毫秒）
     *
     * 从任务开始执行到结束（或超时）的时间。
     * 可用于性能分析和超时阈值调优。
     */
    uint32_t elapsed_ms;

    /* ===== 输出摘要 ===== */

    /** @brief 命令输出的前 N 个字符
     *
     * 截取标准输出的开头部分，便于快速了解执行情况。
     * 完整输出应写入日志文件（如果log_output=true）
     */
    char output_summary[AUTORUN_OUTPUT_SUMMARY_MAX_LEN];

    /* ===== 时间戳 ===== */

    /** @brief 任务开始执行时的系统时间戳（毫秒）
     *
     * 相对于系统启动时间或Epoch（取决于平台实现）
     */
    uint32_t timestamp_start;

    /** @brief 任务结束执行时的系统时间戳（毫秒）*/
    uint32_t timestamp_end;

    /* ===== 重试信息 ===== */

    /** @brief 实际执行次数（首次 + 重试）*/
    uint8_t attempt_count;

    /** @brief 第一次失败的错误码（如果有）*/
    int32_t first_error_code;

    /** @brief 保留字段（对齐）*/
    uint8_t reserved[3];

} task_result_t;

/**
 * @struct autorun_result_t
 * @brief 自动运行结果汇总结构体
 *
 * 位于结果输出区域的起始位置。
 * 包含所有任务的执行结果统计和详细信息。
 *
 * 这是一个变长结构体：
 * - 固定部分: 汇总统计信息
 * - 变长部分: results[] 数组（元素个数 = total_tasks）
 *
 * PC侧读取步骤:
 * 1. 读取固定部分获取 total_tasks
 * 2. 根据 total_tasks 计算 results[] 的大小
 * 3. 读取完整的 results[] 数组
 */
typedef struct {
    /* ===== 头部标识 ===== */

    /** @brief 结果魔数 (必须是 AUTORUN_RESULT_MAGIC)
     *
     * 用于标识这是一个有效的结果区域。
     * PC侧读取时应先检查此值
     */
    uint32_t magic;

    /* ===== 统计信息 ===== */

    /** @brief 任务总数（包括跳过的）*/
    uint32_t total_tasks;

    /** @brief 成功完成的任务数*/
    uint32_t passed_count;

    /** @brief 失败的任务数（FAILED + TIMEOUT + ERROR）*/
    uint32_t failed_count;

    /** @brief 被跳过的任务数*/
    uint32_t skipped_count;

    /** @brief 总体执行状态
     *
     * 0: 全部通过 (passed_count == total_tasks - skipped_count,
     *              且 failed_count == 0)
     * 1: 有失败任务
     */
    uint32_t overall_status;

    /* ===== 失败信息 ===== */

    /** @brief 第一个失败的任务名称
     *
     * 便于快速定位问题。如果是阶段级错误则为空字符串
     */
    char first_failed_task[AUTORUN_TASK_NAME_MAX_LEN];

    /** @brief 第一个失败的任务ID
     *
     * 如果为0xFFFFFFFF表示无失败任务或阶段级错误
     */
    uint32_t first_failed_task_id;

    /* ===== 时间信息 ===== */

    /** @brief 整个流程开始时间戳（毫秒）*/
    uint32_t timestamp_start;

    /** @brief 整个流程结束时间戳（毫秒）*/
    uint32_t timestamp_end;

    /** @brief 总执行耗时（毫秒）*/
    uint32_t total_elapsed_ms;

    /* ===== 阶段信息 ===== */

    /** @brief 已完成的阶段数量*/
    uint32_t completed_stages;

    /** @brief 最后执行的阶段名称*/
    char last_stage[AUTORUN_STAGE_NAME_MAX_LEN];

    /* ===== 详细结果 ===== */

    /**
     * @brief 各任务的详细执行结果（变长数组）
     *
     * 元素个数 = total_tasks
     * 按 task_id 升序排列
     *
     * @note C语言中变长数组必须在结构体末尾
     */
    task_result_t results[];

} autorun_result_t;

/* ================================================================== */
/*                    工具函数声明                                  */
/* ================================================================== */

/**
 * @brief 计算配置数据的CRC32校验值
 *
 * @param[in] data   配置数据指针
 * @param[in] length 数据长度（不包括crc32字段本身的4字节）
 * @return uint32_t  CRC32校验值
 *
 * @note 使用标准CRC32算法 (IEEE 802.3)
 *       多项式: 0x04C11DB7
 *       初始值: 0xFFFFFFFF
 *       反转输入/输出: 是
 */
uint32_t autorun_calc_crc32(const uint8_t *data, uint32_t length);

/**
 * @brief 校验配置数据的有效性
 *
 * @param[in] config 配置数据指针
 * @return int
 *         0 = 有效
 *         -ERR_INVALID_MAGIC = 魔数不匹配
 *         -ERR_VERSION_MISMATCH = 版本不兼容
 *         -ERR_CRC_ERROR = CRC校验失败
 *         -ERR_SIZE_INVALID = 大小无效
 */
int autorun_validate_config(const autorun_config_t *config);

/**
 * @brief 获取配置结构的实际大小
 *
 * 考虑到变长数组，此函数计算实际的内存占用。
 *
 * @param[in] config 配置数据指针
 * @return uint32_t 实际大小（字节）
 */
uint32_t autorun_get_config_size(const autorun_config_t *config);

/**
 * @brief 获取结果结构的实际大小
 *
 * @param[in] result 结果数据指针
 * @return uint32_t 实际大小（字节）
 */
uint32_t autorun_get_result_size(const autorun_result_t *result);

/**
 * @brief 获取指定阶段的配置
 *
 * @param[in]  config     配置数据指针
 * @param[in]  stage_name 阶段名称
 * @return stage_config_t* 阶段配置指针，未找到返回NULL
 */
const stage_config_t* autorun_get_stage_config(
    const autorun_config_t *config,
    const char *stage_name
);

/**
 * @brief 获取指定ID的任务配置
 *
 * @param[in]  config 配置数据指针
 * @param[in]  task_id 任务ID
 * @return task_config_t* 任务配置指针，未找到返回NULL
 */
const task_config_t* autorun_get_task_config(
    const autorun_config_t *config,
    uint32_t task_id
);

/* ================================================================== */
/*                    错误码定义                                    */
/* ================================================================== */

/** @brief 无错误*/
#define AUTORUN_OK                    0

/** @brief 魔数不匹配*/
#define AUTORUN_ERR_INVALID_MAGIC    (-1)

/** @brief 版本不兼容（主版本不同）*/
#define AUTORUN_ERR_VERSION_MISMATCH (-2)

/** @brief CRC32校验失败*/
#define AUTORUN_ERR_CRC_ERROR        (-3)

/** @brief 配置大小无效*/
#define AUTORUN_ERR_SIZE_INVALID     (-4)

/** @brief 参数无效（NULL指针等）*/
#define AUTORUN_ERR_INVALID_PARAM    (-5)

/** @brief 阶段未找到*/
#define AUTORUN_ERR_STAGE_NOT_FOUND  (-6)

/** @brief 任务未找到*/
#define AUTORUN_ERR_TASK_NOT_FOUND   (-7)

/** @brief 内存不足*/
#define AUTORUN_ERR_NO_MEMORY        (-8)

/** @brief 超时*/
#define AUTORUN_ERR_TIMEOUT          (-9)

/** @brief 关键任务失败*/
#define AUTORUN_ERR_CRITICAL_FAIL    (-10)

/** @brief 命令执行错误*/
#define AUTORUN_ERR_EXEC_ERROR       (-11)

/** @brief 结果区域溢出*/
#define AUTORUN_ERR_RESULT_OVERFLOW  (-12)

/** @brief 不支持的特性*/
#define AUTORUN_ERR_NOT_SUPPORTED    (-13)

/** @brief 内部错误*/
#define AUTORUN_ERR_INTERNAL         (-14)

/* ================================================================== */
/*                 编译期配置开关                                   */
/* ================================================================== */

/**
 * @brief 启用调试功能
 *
 * 定义后会包含额外的调试代码和断言
 */
// #define AUTORUN_ENABLE_DEBUG

/**
 * @brief 启用完整日志
 *
 * 未定义时精简日志输出（仅ERROR和关键INFO）
 */
// #define AUTORUN_ENABLE_FULL_LOG

/**
 * @brief 启用结果签名（RSA/ECDSA）
 *
 * 需要密码库支持，增加安全性和抗篡改能力
 */
// #define AUTORUN_ENABLE_SIGNATURE

/**
 * @brief 启用命令白名单
 *
 * 限制可执行的命令范围，提高安全性
 */
// #define AUTORUN_ENABLE_WHITELIST

#ifdef __cplusplus
}
#endif

#endif /* AUTORUN_CONFIG_H */

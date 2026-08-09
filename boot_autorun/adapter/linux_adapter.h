/**
 * @file linux_adapter.h
 * @brief Linux 用户态平台适配层头文件
 *
 * 本文件定义了 Linux 操作系统用户态环境的平台适配接口，
 * 将 Linux 提供的丰富底层能力映射到自动运行框架的标准平台操作表。
 *
 * Linux 环境特点:
 * 1. **运行级别**: 用户态 (User Space, EL0)
 * 2. **内存环境**: 虚拟内存 (MMU完全启用, 可使用malloc/free)
 * 3. **Shell能力**: 完整的 Bash/Sh 命令行 (支持管道、重定向、脚本等)
 * 4. **调试输出**: 通过 syslog / printf / stderr
 * 5. **时间源**: clock_gettime(CLOCK_MONOTONIC) 或 gettimeofday()
 * 6. **并发模型**: 多进程 + 多线程 (pthread)
 * 7. **文件系统**: 完整的 POSIX 文件系统操作
 * 8. **进程管理**: fork/exec/waitpid/pipe/signal
 * 9. **网络**: 完整的 TCP/IP 协议栈 (socket API)
 * 10. **权限**: 用户/组权限, capabilities, SELinux/AppArmor
 *
 * 与其他阶段的关键差异:
 * - 资源最充足: GB级内存可用
 * - 功能最完整: 支持所有POSIX和Linux特有API
 * - 安全性要求: 需要考虑用户权限、沙箱、资源限制
 * - 性能优化空间大: 可以利用多核并行
 * - 日志系统完善: syslog/journald 支持
 * - 进程隔离: 每个命令可在独立进程中执行
 *
 * 高级特性支持:
 * - ✅ 命令输出捕获 (popen/fork+exec+pipe)
 * - ✅ 执行超时控制 (alarm/setitimer)
 * - ✅ 环境变量设置 (setenv/putenv)
 * - ✅ 工作目录切换 (chdir/fchdir)
 * - ✅ 信号处理 (sigaction)
 * - ✅ 资源限制 (getrlimit/setrlimit)
 * - ✅ 文件描述符管理
 * - ✅ 子进程状态收集 (waitpid)
 * - ✅ 管道和重定向
 * - ✅ 后台任务执行
 *
 * 适配策略:
 * - 日志: 使用 printf() / syslog() / vsyslog()
 * - 命令执行: system() (简单) 或 popen()/fork+exec+pipe() (高级)
 * - 时间戳: clock_gettime(CLOCK_MONOTONIC) 高精度
 * - 延时: nanosleep() 或 usleep()
 * - 内存: 标准 malloc/free/memcpy/memset
 * - 文件操作: 标准 fopen/fread/fwrite 或 POSIX open/read/write
 *
 * 典型集成方式:
 * @code
 *   // 方式1: 作为独立程序运行
 *   #include "adapter/linux_adapter.h"
 *
 *   int main(int argc, char *argv[]) {
 *       // 解析命令行参数...
 *
 *       // 执行自动运行任务
 *       return linux_run_autorun();
 *   }
 *
 *   // 方式2: 作为库集成到现有程序
 *   void run_tests(void) {
 *       const autorun_platform_ops_t *ops = linux_get_platform_ops();
 *       // 自定义某些操作...
 *       linux_run_autorun();
 *   }
 *
 *   // 方式3: 使用自定义配置地址 (如从内存映射文件读取)
 *   int main(void) {
 *       void *config = mmap(NULL, config_size, PROT_READ,
 *                           MAP_PRIVATE, fd_config, 0);
 *       void *result = mmap(NULL, result_size, PROT_READ | PROT_WRITE,
 *                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
 *       return linux_run_autorun_with_addr(
 *           (uint32_t)(uintptr_t)config,
 *           (uint32_t)(uintptr_t)result,
 *           result_size
 *       );
 *   }
 * @endcode
 *
 * 配置要求:
 * - 配置地址: 可使用 mmap 映射的文件或动态分配的内存
 * - 结果区域: 动态内存或 mmap 共享内存
 * - 最大配置大小: 建议 < 1MB (Linux 内存充足)
 *
 * @note Linux阶段功能最完善，建议:
 *       1. 执行全面的系统集成测试
 *       2. 利用多进程/多线程提高效率
 *       3. 启用完整的日志记录和分析
 *       4. 使用高级特性 (超时、资源限制、信号处理)
 *       5. 考虑安全性和权限管理
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef LINUX_ADAPTER_H
#define LINUX_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../core/autorun_config.h"

/* ================================================================== */
/*                    平台特定配置                                    */
/* ================================================================== */

/**
 * @brief Linux 阶段自动运行配置的默认地址
 *
 * Linux环境下通常使用动态内存，此默认值仅作为后备方案。
 * 推荐使用 linux_run_autorun_with_mapped() 直接传入指针。
 */
#ifndef PLAT_LINUX_AUTORUN_CONFIG_ADDR
#define PLAT_LINUX_AUTORUN_CONFIG_ADDR    0x40000000UL
#endif

/**
 * @brief Linux 阶段结果输出的默认地址
 */
#ifndef PLAT_LINUX_AUTORUN_RESULT_ADDR
#define PLAT_LINUX_AUTORUN_RESULT_ADDR     0x40400000UL
#endif

/**
 * @brief Linux 阶段结果区域最大大小 (bytes)
 *
 * Linux内存充足，可设置较大的结果区域以记录完整信息。
 * 默认64KB可根据需要调整到MB级别。
 */
#ifndef PLAT_LINUX_AUTORUN_RESULT_SIZE
#define PLAT_LINUX_AUTORUN_RESULT_SIZE     65536
#endif

/**
 * @brief 默认命令执行超时时间 (ms)
 *
 * Linux命令可能需要较长时间（编译、测试套件等）。
 */
#ifndef LINUX_DEFAULT_CMD_TIMEOUT_MS
#define LINUX_DEFAULT_CMD_TIMEOUT_MS       120000
#endif

/**
 * @brief 全局默认超时时间 (ms)
 *
 * 整个Linux阶段的默认超时（30分钟）。
 */
#ifndef LINUX_GLOBAL_TIMEOUT_MS
#define LINUX_GLOBAL_TIMEOUT_MS            1800000
#endif

/* ================================================================== */
/*                   高级配置选项                                     */
/* ================================================================== */

/**
 * @brief Linux 适配器运行选项
 *
 * 提供比基础框架更细粒度的控制选项。
 */
typedef struct {
    /** 是否使用 syslog 记录日志 (false=使用stdout/stderr) */
    bool use_syslog;

    /** syslog 标识符 (仅 use_syslog=true 时有效) */
    char syslog_ident[32];

    /** syslog facility (LOG_USER, LOG_LOCAL0-7等) */
    int syslog_facility;

    /** 工作目录 (空字符串表示不改变) */
    char working_directory[256];

    /** 是否在独立进程中执行每个命令 (true=更安全但稍慢) */
    bool isolate_processes;

    /** 是否设置资源限制 (防止命令耗尽系统资源) */
    bool enable_rlimits;

    /** CPU 时间限制 (秒), 0=不限制 */
    rlim_t cpu_limit;

    /** 内存限制 (字节), 0=不限制 */
    rlim_t memory_limit;

    /** 输出文件大小限制 (字节), 0=不限制 */
    rlim_t output_size_limit;

    /** 是否忽略大小写进行字符串匹配 */
    bool case_insensitive_match;

    /** 保留字段 */
    uint8_t reserved[4];
} linux_adapter_options_t;

/* ================================================================== */
/*                   核心API函数声明                                  */
/* ================================================================== */

/**
 * @brief 初始化并运行Linux阶段的自动任务 (使用默认配置)
 *
 * 这是Linux适配层的简化入口函数，使用默认配置地址和选项。
 *
 * @return int
 *         0 = 所有任务通过
 *         非0 = 有失败任务或初始化错误
 */
int linux_run_autorun(void);

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * @param[in] config_addr 配置数据的物理地址 (或虚拟地址)
 * @param[in] result_addr 结果区域的物理地址 (或虚拟地址)
 * @param[in] result_size 结果区域大小 (bytes)
 *
 * @return int 0=成功, 非0=错误
 */
int linux_run_autorun_with_addr(uint32_t config_addr,
                                uint32_t result_addr,
                                uint32_t result_size);

/**
 * @brief 使用已映射的指针运行自动任务 (推荐用于Linux)
 *
 * 此函数接受直接指针而非地址，更适合Linux的虚拟内存模型。
 * 配置和结果数据可通过以下方式获取:
 * - 从文件 mmap
 * - malloc 分配
 * - 共享内存 (shm_open)
 *
 * @param[in] config      配置数据指针 (已加载到内存)
 * @param[in] config_size 配置数据大小 (bytes)
 * @param[out] result     结果输出缓冲区 (预分配)
 * @param[in] result_size 结果缓冲区大小 (bytes)
 *
 * @return int 0=成功, 非0=错误
 */
int linux_run_autorun_with_mapped(const autorun_config_t *config,
                                  uint32_t config_size,
                                  autorun_result_t *result,
                                  uint32_t result_size);

/**
 * @brief 使用自定义选项运行自动任务
 *
 * @param[in] config      配置数据指针
 * @param[in] config_size 配置数据大小
 * @param[out] result     结果缓冲区
 * @param[in] result_size 结果缓冲区大小
 * @param[in] options     Linux适配器选项 (NULL则使用默认值)
 *
 * @return int 0=成功, 非0=错误
 */
int linux_run_autorun_ex(const autorun_config_t *config,
                         uint32_t config_size,
                         autorun_result_t *result,
                         uint32_t result_size,
                         const linux_adapter_options_t *options);

/**
 * @brief 获取Linux适配器的平台操作表
 *
 * 允许外部模块访问和修改平台操作函数表。
 *
 * @return const autorun_platform_ops_t* 平台操作表指针 (静态存储)
 */
const autorun_platform_ops_t* linux_get_platform_ops(void);

/**
 * @brief 获取Linux适配器的扩展选项
 *
 * 返回当前使用的Linux特定选项，可用于查询或修改。
 *
 * @return linux_adapter_options_t* 选项结构体指针
 */
linux_adapter_options_t* linux_get_adapter_options(void);

/* ================================================================== */
/*                    状态查询                                       */
/* ================================================================== */

/**
 * @brief 检查Linux自动运行是否已完成
 *
 * @return bool true=已完成, false=未执行或正在执行
 */
bool linux_is_autorun_completed(void);

/**
 * @brief 获取最后一次执行的返回码
 *
 * @return int 最后一次 linux_run_autorun() 的返回值
 */
int linux_get_last_result(void);

/**
 * @brief 获取最后一次执行的详细统计信息
 *
 * @param[out] stats 统计信息输出结构体
 * @return int 0=成功, -1=无统计数据
 */
int linux_get_statistics(linux_statistics_t *stats);

/* ================================================================== */
/*                    扩展统计信息                                   */
/* ================================================================== */

/**
 * @brief Linux 特有的统计信息
 */
typedef struct {
    /** 总计执行的子进程数 */
    uint32_t total_forked;

    /** 因超时被杀死的子进程数 */
    uint32_t timeout_kills;

    /** 因信号终止的子进程数 */
    uint32_t signal_terminations;

    /** 总CPU时间消耗 (毫秒) */
    uint64_t total_cpu_time_ms;

    /** 峰值内存使用量 (KB) */
    size_t peak_memory_kb;

    /** 最大的单个命令输出大小 (字节) */
    size_t max_output_size;

    /** 平均命令执行时间 (毫秒) */
    uint32_t avg_cmd_time_ms;

} linux_statistics_t;

/* ================================================================== */
/*                    工具函数                                       */
/* ================================================================== */

/**
 * @brief 设置环境变量并执行命令 (原子操作)
 *
 * 在临时修改环境变量的情况下执行命令，执行后恢复原环境。
 *
 * @param[in] command     要执行的命令
 * @param[in] env_vars    环境变量数组 ("KEY=VALUE" 格式)
 * @param[in] env_count   环境变量数量
 * @param[out] output     输出缓冲区
 * @param[in] output_size 输出缓冲区大小
 * @param[in] timeout_ms  超时时间 (ms)
 *
 * @return int 命令返回码
 */
int linux_exec_with_env(const char *command,
                        const char *env_vars[],
                        int env_count,
                        char *output,
                        size_t output_size,
                        uint32_t timeout_ms);

/**
 * @brief 在指定工作目录中执行命令
 *
 * @param[in] command     要执行的命令
 * @param[in] work_dir    工作目录路径
 * @param[out] output     输出缓冲区
 * @param[in] output_size 输出缓冲区大小
 * @param[in] timeout_ms  超时时间 (ms)
 *
 * @return int 命令返回码
 */
int linux_exec_in_directory(const char *command,
                            const char *work_dir,
                            char *output,
                            size_t output_size,
                            uint32_t timeout_ms);

/**
 * @brief 执行命令并写入日志文件
 *
 * 将命令输出同时捕获到缓冲区和追加到指定日志文件。
 *
 * @param[in] command     要执行的命令
 * @param[in] log_file    日志文件路径
 * @param[out] output     输出缓冲区 (可为NULL)
 * @param[in] output_size 输出缓冲区大小
 * @param[in] timeout_ms  超时时间 (ms)
 *
 * @return int 命令返回码
 */
int linux_exec_with_logging(const char *command,
                            const char *log_file,
                            char *output,
                            size_t output_size,
                            uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LINUX_ADAPTER_H */

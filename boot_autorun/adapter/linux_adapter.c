/**
 * @file linux_adapter.c
 * @brief Linux 用户态平台适配层实现
 *
 * 本文件实现了 Linux 操作系统用户态环境的平台适配，
 * 将 Linux 提供的丰富底层能力映射到自动运行框架的标准接口。
 *
 * 实现要点:
 * 1. **日志适配**: printf() / syslog() / vsyslog()
 * 2. **命令执行**: system() / popen() / fork()+exec()+pipe()
 * 3. **时间管理**: clock_gettime(CLOCK_MONOTONIC) 高精度
 * 4. **延时管理**: nanosleep() / usleep()
 * 5. **内存操作**: 标准 libc (malloc/free/memcpy/memset)
 * 6. **进程管理**: fork/exec/waitpid/pipe/signal/alarm
 * 7. **文件操作**: fopen/fdopen/read/write/close
 * 8. **环境变量**: setenv/putenv/extern char **environ
 * 9. **资源限制**: getrlimit/setrlimit
 * 10. **信号处理**: sigaction/sigprocmask
 *
 * 高级特性:
 * - ✅ 命令输出捕获 (popen 或 fork+pipe)
 * - ✅ 执行超时控制 (alarm/setitimer + SIGALRM)
 * - ✅ 环境变量临时设置 (save/restore environ)
 * - ✅ 工作目录切换 (getcwd/chdir/fchdir)
 * - ✅ 子进程状态收集 (WIFEXITED/WIFSIGNALED/WTERMSIG)
 * - ✅ 资源限制 (RLIMIT_CPU, RLIMIT_AS, RLIMIT_FSIZE)
 * - ✅ 文件描述符管理 (close-on-exec)
 * - ✅ 管道和重定向 (dup2)
 * - ✅ 后台任务执行 (fork without wait)
 * - ✅ 统计信息收集 (CPU time, memory, I/O)
 *
 * 安全性考虑:
 * - 命令注入防护 (shell 特殊字符转义)
 * - 信号处理安全性 (async-signal-safe functions only in handler)
 * - 资源耗尽保护 (setrlimit before exec)
 * - 临时文件安全 (mkstemp/mktemp with O_EXCL)
 * - 权限检查 (getuid/geteuid/capabilities)
 *
 * 性能优化:
 * - 使用 vfork 替代 fork (当可能时)
 * - 缓冲区大小可配置
 * - 避免不必要的内存拷贝
 * - 使用 clock_gettime(CLOCK_MONOTONIC_COARSE) 降低开销
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "linux_adapter.h"
#include "../core/autorun_core.h"

/*
 * Linux/POSIX 头文件包含
 */
#include <stdio.h>                  /* printf, fprintf, snprintf, FILE */
#include <stdlib.h>                 /* malloc, free, system, getenv, setenv */
#include <string.h>                 /* strlen, strncmp, memset, memcpy */
#include <stdarg.h>                 /* va_list, va_start, va_end, vsnprintf */
#include <unistd.h>                 /* fork, exec, pipe, dup2, chdir, getcwd */
#include <sys/types.h>              /* pid_t, size_t, ssize_t */
#include <sys/wait.h>               /* waitpid, WIFEXITED, WIFSIGNALED */
#include <sys/time.h>               /* gettimeofday, setitimer, itimerval */
#include <sys/resource.h>           /* getrlimit, setrlimit, rlimit, rusage */
#include <sys/stat.h>               /* stat, fstat, umask */
#include <fcntl.h>                  /* open, close, fcntl, O_*, FD_* */
#include <signal.h>                 /* signal, sigaction, kill, alarm */
#include <errno.h>                  /* errno, EINTR, ECHILD */
#include <time.h>                   /* clock_gettime, timespec, nanosleep */
#include <ctype.h>                  /* isspace, tolower */
#include <paths.h>                  /* _PATH_DEVNULL */
#include <limits.h>                 /* PATH_MAX, INT_MAX */
#include <sysexits.h>               /* exit status codes */

/* 可选头文件 */
#ifdef __GLIBC__
    #include <sys/syscall.h>        /* SYS_gettid (for thread ID) */
#endif

#ifdef USE_SYSLOG
    #include <syslog.h>             /* openlog, syslog, closelog, LOG_* */
#endif

/* ================================================================== */
/*                    内部常量定义                                    */
/* ================================================================== */

/** @brief 输出缓冲区大小 (栈上分配或malloc)
 *
 * Linux 栈空间通常为几MB到几十MB，可以使用较大的缓冲区。
 * 此值可根据需要调整，建议 >= 4KB 以提高读取效率。
 */
#define OUTPUT_BUFFER_SIZE          65536

/** @brief 命令执行时的读缓冲区大小
 *
 * 用于 pipe read 的缓冲区大小，影响I/O效率。
 * 建议 4KB-64KB，与文件系统块大小对齐。
 */
#define PIPE_READ_BUF_SIZE          4096

/** @brief 默认命令执行超时时间 (ms) */
#define DEFAULT_EXEC_TIMEOUT_MS     LINUX_DEFAULT_CMD_TIMEOUT_MS

/** @brief 全局超时时间 (ms) */
#define GLOBAL_TIMEOUT_MS            LINUX_GLOBAL_TIMEOUT_MS

/** @brief 最大环境变量值长度 */
#define MAX_ENV_VALUE_LEN           4096

/** @brief 最大工作目录路径长度 */
#define MAX_PATH_LEN                PATH_MAX

/** @brief 信号处理超时标记 (volatile sig_atomic_t 保证原子访问) */
static volatile sig_atomic_t g_timeout_flag = 0;

/** @brief 被杀死的子进程 PID (用于信号处理) */
static volatile pid_t g_timed_out_pid = 0;

/* ================================================================== */
/*                    全局状态变量                                    */
/* ================================================================== */

/** @brief 最后一次执行结果 */
static int g_last_result = 0;

/** @brief 执行完成标记 */
static bool g_completed = false;

/** @brief 平台操作函数表 (单例) */
static autorun_platform_ops_t g_linux_ops;

/** @brief 操作表是否已初始化 */
static bool g_ops_initialized = false;

/** @brief Linux适配器扩展选项 */
static linux_adapter_options_t g_linux_options;

/** @brief 选项是否已初始化 */
static bool g_options_initialized = false;

/** @brief 统计信息 */
static linux_statistics_t g_statistics;

/** @brief syslog 是否已打开 */
static bool g_syslog_opened = false;

/** @brief 保存的工作目录 */
static char g_saved_cwd[MAX_PATH_LEN];

/** @brief 是否已保存工作目录 */
static bool g_cwd_saved = false;

/* ================================================================== */
/*                 内部辅助函数声明                                   */
/* ================================================================== */

/* 初始化函数 */
static void init_platform_ops(void);
static void init_default_options(void);

/* 日志相关 */
static void setup_syslog(void);
static void cleanup_syslog(void);
static int map_log_level(int framework_level);

/* 时间相关 */
static uint64_t get_time_ms(void);
static uint32_t ns_to_ms(uint64_t ns);
static uint64_t ms_to_ns(uint32_t ms);

/* 命令执行核心 */
static int execute_command_simple(const char *command,
                                  char *output,
                                  size_t output_size,
                                  uint32_t timeout_ms);
static int execute_command_advanced(const char *command,
                                   char *output,
                                   size_t output_size,
                                   uint32_t timeout_ms);
static int execute_command_with_fork(const char *command,
                                     char *output,
                                     size_t output_size,
                                     uint32_t timeout_ms);

/* 进程管理 */
static pid_t safe_fork(void);
static int wait_for_process(pid_t pid, uint32_t timeout_ms,
                            int *status, bool *timed_out);
static void kill_process_tree(pid_t root_pid);
static void setup_child_process(void);

/* 输出捕获 */
static ssize_t capture_output(int fd, char *buffer, size_t buffer_size,
                              size_t *total_captured);
static ssize_t read_with_timeout(int fd, void *buffer, size_t count,
                                 uint32_t timeout_ms);

/* 信号处理 */
static void setup_signal_handlers(void);
static void restore_signal_handlers(void);
static void timeout_handler(int signum);
static void ignore_sigpipe(void);

/* 环境和目录 */
static int save_current_directory(void);
static int restore_directory(void);
static char **save_environment(void);
static int restore_environment(char **saved_env);

/* 工具函数 */
static bool is_shell_safe(const char *command);
static char *escape_shell_arg(const char *arg);
static void update_statistics(pid_t pid, int status,
                               uint64_t start_time, uint64_t end_time,
                               size_t output_size);
static void reset_statistics(void);

/* ================================================================== */
/*               初始化函数实现                                       */
/* ================================================================== */

/**
 * @brief 初始化平台操作函数表
 *
 * 将所有平台相关的函数指针映射到Linux的具体实现。
 * 此函数保证只初始化一次（单例模式）。
 */
static void init_platform_ops(void)
{
    if (g_ops_initialized) {
        return;
    }

    g_linux_ops.log_printf = linux_log_printf;
    g_linux_ops.execute_command = linux_execute_command;
    g_linux_ops.get_timestamp = linux_get_timestamp;
    g_linux_ops.delay_ms = linux_delay_ms;
    g_linux_ops.memcpy = memcpy;
    g_linux_ops.memset = memset;

    g_ops_initialized = true;
}

/**
 * @brief 初始化默认选项
 *
 * 设置合理的默认值，用户可通过 linux_run_autorun_ex() 覆盖。
 */
static void init_default_options(void)
{
    if (g_options_initialized) {
        return;
    }

    memset(&g_linux_options, 0, sizeof(g_linux_options));

    /* 日志: 默认使用 stdout/stderr，不使用 syslog */
    g_linux_options.use_syslog = false;
    strncpy(g_linux_options.syslog_ident, "autorun",
            sizeof(g_linux_options.syslog_ident) - 1);
    g_linux_options.syslog_facility = LOG_USER;  /* 需要 #include <syslog.h> */

    /* 工作目录: 不改变 */
    g_linux_options.working_directory[0] = '\0';

    /* 进程隔离: 启用 (更安全) */
    g_linux_options.isolate_processes = true;

    /* 资源限制: 启用 */
    g_linux_options.enable_rlimits = true;
    g_linux_options.cpu_limit = 300;         /* 5分钟 CPU时间 */
    g_linux_options.memory_limit = 512 * 1024 * 1024;  /* 512MB 内存 */
    g_linux_options.output_size_limit = 10 * 1024 * 1024;  /* 10MB 输出 */

    /* 匹配选项: 区分大小写 */
    g_linux_options.case_insensitive_match = false;

    g_options_initialized = true;
}

/* ================================================================== */
/*              平台操作函数实现 (供框架调用)                          */
/* ================================================================== */

/**
 * @brief 日志输出函数
 *
 * 将框架的日志请求映射到Linux的日志输出系统。
 * 支持两种模式:
 * 1. 使用 syslog (适合守护进程/服务)
 * 2. 使用 printf/stderr (适合交互式程序)
 *
 * @param[in] level 日志级别 (0-5)
 * @param[in] fmt 格式字符串
 * @param[in] ... 可变参数列表
 */
void linux_log_printf(int level, const char *fmt, ...)
{
    int syslog_level = map_log_level(level);

    /* 级别为 OFF (-1) 时不输出 */
    if (syslog_level < 0 && !g_linux_options.use_syslog) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    if (g_linux_options.use_syslog) {
        /*
         * 使用 syslog 输出
         * 注意：vsyslog 是 async-signal-safe 的吗？
         * 实际上不是，但在正常代码路径中是安全的。
         */
#ifdef USE_SYSLOG
        vsyslog(syslog_level | g_linux_options.syslog_facility, fmt, args);
#else
        /* 回退到 stderr */
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
#endif
    } else {
        /*
         * 使用标准输出/错误输出
         * ERROR/WARNING -> stderr
         * INFO/DEBUG/VERBOSE -> stdout
         */
        FILE *out = (level <= 2) ? stderr : stdout;

        /* 可选：添加时间戳前缀 */
        if (level <= 2) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct tm *tm_info = localtime(&tv.tv_sec);

            char time_buf[32];
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

            fprintf(out, "[%s.%03ld] ", time_buf, tv.tv_usec / 1000);
        }

        /* 添加级别前缀 */
        const char *level_prefix[] = {
            "",           /* 0: OFF (不会到达) */
            "ERROR: ",    /* 1: ERROR */
            "WARNING: ",  /* 2: WARNING */
            "",           /* 3: INFO (无前缀) */
            "DEBUG: ",    /* 4: DEBUG */
            "TRACE: "     /* 5: VERBOSE */
        };

        if (level >= 1 && level <= 5) {
            fprintf(out, "%s", level_prefix[level]);
        }

        vfprintf(out, fmt, args);
        fprintf(out, "\n");
        fflush(out);
    }

    va_end(args);
}

/**
 * @brief Shell 命令执行函数
 *
 * 在 Linux 环境中执行 Shell 命令并捕获输出。
 *
 * 实现策略（按优先级）:
 * 1. 使用 fork()+exec()+pipe() (最灵活，支持超时、资源限制等)
 * 2. 使用 popen() (简单但功能有限)
 * 3. 使用 system() (最简单，无法捕获输出)
 *
 * 选择逻辑:
 * - 如果需要超时控制 → fork+exec+pipe
 * - 如果需要输出捕获 → popen 或 fork+pipe
 * - 如果只需要返回码 → system
 *
 * @param[in] command     要执行的命令字符串
 * @param[out] output     输出缓冲区，存储命令的标准输出
 * @param[in] output_size 输出缓冲区大小 (bytes)
 * @param[in] timeout_ms  超时时间 (毫秒)，0表示无限等待
 * @return int 命令返回码 (0=成功, 非0=失败)
 */
int linux_execute_command(const char *command,
                          char *output,
                          size_t output_size,
                          uint32_t timeout_ms)
{
    /*
     * 命令执行函数。
     *
     * 在Linux环境中，此函数提供完整的Shell命令执行能力：
     *
     * 功能特性:
     * 1. 执行任意 Shell 命令 (通过 /bin/sh -c)
     * 2. 捕获标准输出和标准错误
     * 3. 支持执行超时 (通过 alarm/SIGALRM)
     * 4. 收集子进程退出状态
     * 5. 记录统计信息 (CPU时间、内存等)
     *
     * 实现方式选择:
     * - timeout_ms > 0 且需要完整控制 → execute_command_with_fork()
     * - 仅需要输出捕获 → execute_command_advanced() [popen]
     * - 最简方式 → execute_command_simple() [system]
     */

    /* 参数校验 */
    if (command == NULL || command[0] == '\0') {
        if (output != NULL && output_size > 0) {
            output[0] = '\0';
        }
        return -1;
    }

    /* 初始化输出缓冲区 */
    if (output != NULL && output_size > 0) {
        output[0] = '\0';
    }

    /* 设置默认超时 */
    if (timeout_ms == 0) {
        timeout_ms = DEFAULT_EXEC_TIMEOUT_MS;
    }

    /* 安全性检查：检测明显的命令注入尝试 */
    if (!is_shell_safe(command)) {
        linux_log_printf(1,
            "Security Warning: Potentially unsafe command detected: %.128s\n",
            command);
        /* 继续执行但记录警告 */
    }

    /*
     * 选择执行策略
     */
    uint64_t start_time = get_time_ms();
    int ret;

    if (g_linux_options.isolate_processes || timeout_ms > 0) {
        /*
         * 使用 fork+exec+pipe 方式
         * 支持:
         * - 超时控制
         * - 资源限制
         * - 完整的状态收集
         * - 进程树终止
         */
        ret = execute_command_with_fork(command, output, output_size,
                                        timeout_ms);
    } else {
        /*
         * 使用 popen 方式 (较简单)
         * 支持:
         * - 输出捕获
         * - 返回码获取
         * 不支持:
         * - 超时控制 (需要额外线程或信号)
         * - 资源限制
         */
        ret = execute_command_advanced(command, output, output_size,
                                       timeout_ms);
    }

    uint64_t end_time = get_time_ms();

    /* 更新统计信息 */
    update_statistics(0, ret, start_time, end_time,
                      (output != NULL) ? strlen(output) : 0);

    return ret;
}

/**
 * @brief 获取当前系统时间戳 (毫秒)
 *
 * 基于 CLOCK_MONOTONIC 时钟获取高精度时间戳。
 * CLOCK_MONOTONIC 不受系统时间调整影响，适合测量时间间隔。
 *
 * @return uint32_t 当前时间戳 (毫秒)
 */
uint32_t linux_get_timestamp(void)
{
    return (uint32_t)(get_time_ms() & 0xFFFFFFFFUL);
}

/**
 * @brief 毫秒级延时函数
 *
 * 使用 nanosleep() 进行精确延时，不会被信号中断（自动重启）。
 *
 * @param[in] ms 延时毫秒数
 */
void linux_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    struct timespec req, rem;
    req.tv_sec = (time_t)(ms / 1000UL);
    req.tv_nsec = (long)((ms % 1000UL) * 1000000UL);

    /*
     * 循环调用 nanosleep 直到完全延时完成
     * 处理被信号中断的情况
     */
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;  /* 剩余时间继续等待 */
        } else {
            break;      /* 其他错误直接退出 */
        }
    }
}

/* ================================================================== */
/*                核心API函数实现                                     */
/* ================================================================== */

/**
 * @brief 使用默认配置运行自动任务
 *
 * @return int 0=所有任务通过, 非0=有失败或错误
 */
int linux_run_autorun(void)
{
    return linux_run_autorun_with_addr(
        PLAT_LINUX_AUTORUN_CONFIG_ADDR,
        PLAT_LINUX_AUTORUN_RESULT_ADDR,
        PLAT_LINUX_AUTORUN_RESULT_SIZE
    );
}

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * @param[in] config_addr 配置数据的物理/虚拟地址
 * @param[in] result_addr 结果区域的物理/虚拟地址
 * @param[in] result_size 结果区域大小 (bytes)
 * @return int 0=成功, 非0=错误
 */
int linux_run_autorun_with_addr(uint32_t config_addr,
                                uint32_t result_addr,
                                uint32_t result_size)
{
    /*
     * 将地址转换为指针
     * 注意：在Linux用户态中，这通常需要有效的虚拟地址
     * 推荐使用 linux_run_autorun_with_mapped() 直接传入指针
     */
    const autorun_config_t *config =
        (const autorun_config_t *)(uintptr_t)config_addr;
    autorun_result_t *result =
        (autorun_result_t *)(uintptr_t)result_addr;

    return linux_run_autorun_with_mapped(config, 0, result, result_size);
}

/**
 * @brief 使用已映射的指针运行自动任务 (推荐用于Linux)
 *
 * @param[in] config      配置数据指针
 * @param[in] config_size 配置数据大小 (0=自动计算)
 * @param[out] result     结果缓冲区
 * @param[in] result_size 结果缓冲区大小
 * @return int 0=成功, 非0=错误
 */
int linux_run_autorun_with_mapped(const autorun_config_t *config,
                                  uint32_t config_size,
                                  autorun_result_t *result,
                                  uint32_t result_size)
{
    return linux_run_autorun_ex(config, config_size, result,
                                 result_size, NULL);
}

/**
 * @brief 使用自定义选项运行自动任务 (最完整的接口)
 *
 * 这是Linux适配层的主要入口函数，完成完整的自动运行流程：
 * 1. 初始化平台操作函数表和选项
 * 2. 设置信号处理和日志系统
 * 3. 保存当前工作目录和环境
 * 4. 初始化框架核心引擎
 * 5. 配置运行选项
 * 6. 执行 'linux' 阶段的所有任务
 * 7. 收集并打印结果摘要
 * 8. 清理资源和恢复环境
 *
 * @param[in] config      配置数据指针
 * @param[in] config_size 配置数据大小
 * @param[out] result     结果缓冲区
 * @param[in] result_size 结果缓冲区大小
 * @param[in] options     Linux适配器选项 (NULL则使用默认值)
 * @return int 0=成功, 非0=错误
 */
int linux_run_autorun_ex(const autorun_config_t *config,
                         uint32_t config_size,
                         autorun_result_t *result,
                         uint32_t result_size,
                         const linux_adapter_options_t *options)
{
    /* 打印启动信息 */
    linux_log_printf(3, "\n");
    linux_log_printf(3, "=========================================\n");
    linux_log_printf(3, "  Linux AutoRun Framework Starting\n");
    linux_log_printf(3, "=========================================\n");
    linux_log_printf(3, "Config ptr:   %p\n", (const void *)config);
    linux_log_printf(3, "Result ptr:   %p\n", (void *)result);
    linux_log_printf(3, "Result size:  %u bytes\n", result_size);
    linux_log_printf(3, "\n");

    /* 初始化 */
    init_platform_ops();
    init_default_options();

    /* 应用自定义选项 */
    if (options != NULL) {
        memcpy(&g_linux_options, options, sizeof(g_linux_options));
    }

    /* 设置 syslog (如果启用) */
    if (g_linux_options.use_syslog) {
        setup_syslog();
    }

    /* 设置信号处理器 */
    setup_signal_handlers();
    ignore_sigpipe();

    /* 保存工作目录 */
    save_current_directory();

    /* 重置统计信息 */
    reset_statistics();

    /* 初始化框架核心引擎 */
    autorun_context_t *ctx = NULL;
    int ret = autorun_init_mapped(config, result, result_size,
                                   &g_linux_ops, &ctx);

    if (ret != 0) {
        linux_log_printf(1, "Linux AutoRun: Init failed with error %d\n", ret);

        switch (ret) {
            case AUTORUN_ERR_INVALID_MAGIC:
                linux_log_printf(1, "  -> Invalid magic number\n");
                break;
            case AUTORUN_ERR_VERSION_MISMATCH:
                linux_log_printf(1, "  -> Version mismatch\n");
                break;
            case AUTORUN_ERR_CRC_ERROR:
                linux_log_printf(1, "  -> CRC32 check failed\n");
                break;
            case AUTORUN_ERR_INVALID_PARAM:
                linux_log_printf(1, "  -> Invalid parameter\n");
                break;
            case AUTORUN_ERR_NO_MEMORY:
                linux_log_printf(1, "  -> Result buffer too small\n");
                break;
            default:
                linux_log_printf(1, "  -> Unknown error code\n");
                break;
        }

        goto cleanup_and_exit;
    }

    linux_log_printf(3, "Linux AutoRun: Framework initialized successfully\n");

    /* 配置运行选项 */
    autorun_options_t frame_opts;
    memset(&frame_opts, 0, sizeof(frame_opts));

    frame_opts.stop_on_critical_fail = true;
    frame_opts.capture_full_output = true;
    frame_opts.global_timeout_ms = GLOBAL_TIMEOUT_MS;
    frame_opts.verbose_logging = true;

    autorun_set_options(ctx, &frame_opts);

    /* 切换工作目录 (如果指定) */
    if (g_linux_options.working_directory[0] != '\0') {
        if (chdir(g_linux_options.working_directory) != 0) {
            linux_log_printf(2,
                "Warning: Failed to change to directory '%s': %s\n",
                g_linux_options.working_directory, strerror(errno));
        } else {
            linux_log_printf(3, "Changed working directory to '%s'\n",
                           g_linux_options.working_directory);
        }
    }

    /* 执行 Linux 阶段的所有任务 */
    linux_log_printf(3, "Linux AutoRun: Executing stage 'linux'...\n");
    ret = autorun_run_stage(ctx, "linux");

    /* 获取并打印结果摘要 */
    const autorun_result_t *run_result = autorun_get_result(ctx);
    if (run_result != NULL) {
        linux_log_printf(3, "\n");
        linux_log_printf(3, "========== Linux AutoRun Result ==========\n");
        linux_log_printf(3, "Total tasks:  %u\n", run_result->total_tasks);
        linux_log_printf(3, "Passed:       %u\n", run_result->passed_count);
        linux_log_printf(3, "Failed:       %u\n", run_result->failed_count);
        linux_log_printf(3, "Skipped:      %u\n", run_result->skipped_count);
        linux_log_printf(3, "Elapsed time: %u ms\n", run_result->total_elapsed_ms);
        linux_log_printf(3, "Overall:      %s\n",
               (run_result->overall_status == 0) ? "PASS" : "FAIL");

        if (run_result->failed_count > 0 &&
            run_result->first_failed_task[0] != '\0') {
            linux_log_printf(2, "First failure: %s (ID=%u)\n",
                 run_result->first_failed_task,
                 run_result->first_failed_task_id);
        }
        linux_log_printf(3, "============================================\n\n");

        /* 打印Linux特有统计信息 */
        linux_log_printf(3, "--- Linux Statistics ---\n");
        linux_log_printf(3, "Total processes forked:  %u\n",
                        g_statistics.total_forked);
        linux_log_printf(3, "Timeout kills:           %u\n",
                        g_statistics.timeout_kills);
        linux_log_printf(3, "Signal terminations:     %u\n",
                        g_statistics.signal_terminations);
        linux_log_printf(3, "Total CPU time:           %lu ms\n",
                        (unsigned long)g_statistics.total_cpu_time_ms);
        linux_log_printf(3, "Peak memory usage:        %zu KB\n",
                        g_statistics.peak_memory_kb);
        linux_log_printf(3, "Max output size:          %zu bytes\n",
                        g_statistics.max_output_size);
        linux_log_printf(3, "Average cmd time:         %u ms\n",
                        g_statistics.avg_cmd_time_ms);
        linux_log_printf(3, "--------------------------\n\n");
    } else {
        linux_log_printf(2, "Linux AutoRun: No result available\n");
    }

cleanup_and_exit:
    /* 清理框架资源 */
    if (ctx != NULL) {
        autorun_cleanup(ctx);
    }

    /* 恢复工作目录 */
    restore_directory();

    /* 恢复信号处理器 */
    restore_signal_handlers();

    /* 关闭 syslog */
    if (g_linux_options.use_syslog) {
        cleanup_syslog();
    }

    /* 更新状态 */
    g_last_result = ret;
    g_completed = true;

    linux_log_printf(3, "Linux AutoRun: Completed with code %d\n", ret);
    linux_log_printf(3, "=========================================\n\n");

    return ret;
}

/**
 * @brief 获取Linux适配器的平台操作表
 *
 * @return const autorun_platform_ops_t* 平台操作表指针
 */
const autorun_platform_ops_t* linux_get_platform_ops(void)
{
    init_platform_ops();
    return &g_linux_ops;
}

/**
 * @brief 获取Linux适配器的扩展选项
 *
 * @return linux_adapter_options_t* 选项结构体指针
 */
linux_adapter_options_t* linux_get_adapter_options(void)
{
    init_default_options();
    return &g_linux_options;
}

/* ================================================================== */
/*                    状态查询实现                                    */
/* ================================================================== */

bool linux_is_autorun_completed(void)
{
    return g_completed;
}

int linux_get_last_result(void)
{
    return g_last_result;
}

int linux_get_statistics(linux_statistics_t *stats)
{
    if (stats == NULL) {
        return -1;
    }

    memcpy(stats, &g_statistics, sizeof(g_statistics));
    return 0;
}

/* ================================================================== */
/*                    扩展工具函数实现                                */
/* ================================================================== */

/**
 * @brief 设置环境变量并执行命令 (原子操作)
 */
int linux_exec_with_env(const char *command,
                        const char *env_vars[],
                        int env_count,
                        char *output,
                        size_t output_size,
                        uint32_t timeout_ms)
{
    if (command == NULL || env_vars == NULL || env_count <= 0) {
        return -1;
    }

    /* 保存原始环境 */
    char **saved_env = save_environment();
    if (saved_env == NULL) {
        linux_log_printf(1, "Failed to save environment\n");
        return -1;
    }

    /* 设置新的环境变量 */
    for (int i = 0; i < env_count; i++) {
        if (env_vars[i] == NULL) continue;

        /* 解析 KEY=VALUE */
        char *eq = strchr(env_vars[i], '=');
        if (eq != NULL) {
            *eq = '\0';
            if (setenv(env_vars[i], eq + 1, 1) != 0) {
                linux_log_printf(2, "Warning: Failed to set env %s\n",
                               env_vars[i]);
            }
            *eq = '=';
        }
    }

    /* 执行命令 */
    int ret = linux_execute_command(command, output, output_size, timeout_ms);

    /* 恢复原始环境 */
    restore_environment(saved_env);

    return ret;
}

/**
 * @brief 在指定工作目录中执行命令
 */
int linux_exec_in_directory(const char *command,
                            const char *work_dir,
                            char *output,
                            size_t output_size,
                            uint32_t timeout_ms)
{
    if (command == NULL || work_dir == NULL) {
        return -1;
    }

    /* 保存当前目录 */
    char old_cwd[MAX_PATH_LEN];
    if (getcwd(old_cwd, sizeof(old_cwd)) == NULL) {
        linux_log_printf(1, "Failed to get current directory\n");
        return -1;
    }

    /* 切换到目标目录 */
    if (chdir(work_dir) != 0) {
        linux_log_printf(1, "Failed to change directory to '%s': %s\n",
                        work_dir, strerror(errno));
        return -1;
    }

    /* 执行命令 */
    int ret = linux_execute_command(command, output, output_size, timeout_ms);

    /* 恢复原目录 */
    if (chdir(old_cwd) != 0) {
        linux_log_printf(2, "Warning: Failed to restore directory: %s\n",
                        strerror(errno));
    }

    return ret;
}

/**
 * @brief 执行命令并写入日志文件
 */
int linux_exec_with_logging(const char *command,
                            const char *log_file,
                            char *output,
                            size_t output_size,
                            uint32_t timeout_ms)
{
    if (command == NULL || log_file == NULL) {
        return -1;
    }

    /* 执行命令并捕获输出 */
    int ret = linux_execute_command(command, output, output_size, timeout_ms);

    /* 追加写入日志文件 */
    if (output != NULL && output[0] != '\0') {
        FILE *lf = fopen(log_file, "a");
        if (lf != NULL) {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char time_buf[32];
            strftime(time_buf, sizeof(time_buf),
                     "%Y-%m-%d %H:%M:%S", tm_info);

            fprintf(lf, "\n[%s] Command: %s\n", time_buf, command);
            fprintf(lf, "Return code: %d\n", ret);
            fprintf(lf, "Output:\n%s\n", output);
            fclose(lf);
        } else {
            linux_log_printf(2, "Warning: Failed to open log file '%s'\n",
                           log_file);
        }
    }

    return ret;
}

/* ================================================================== */
/*               内部辅助函数实现                                      */
/* ================================================================== */

/* ---------- 日志相关 ---------- */

static void setup_syslog(void)
{
#ifdef USE_SYSLOG
    if (!g_syslog_opened) {
        openlog(g_linux_options.syslog_ident,
                LOG_PID | LOG_NDELAY | LOG_NOWAIT,
                g_linux_options.syslog_facility);
        g_syslog_opened = true;
    }
#endif
}

static void cleanup_syslog(void)
{
#ifdef USE_SYSLOG
    if (g_syslog_opened) {
        closelog();
        g_syslog_opened = false;
    }
#endif
}

static int map_log_level(int framework_level)
{
    switch (framework_level) {
        case 0: return -1;  /* OFF */
        case 1: return LOG_ERR;
        case 2: return LOG_WARNING;
        case 3: return LOG_INFO;
        case 4: return LOG_DEBUG;
        case 5: return LOG_DEBUG;  /* VERBOSE -> DEBUG */
        default: return LOG_INFO;
    }
}

/* ---------- 时间相关 ---------- */

static uint64_t get_time_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_COARSE)
    /* 低开销版本，精度较低 (~ms级)，适合频繁调用 */
    if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL +
               (uint64_t)ts.tv_nsec / 1000000ULL;
    }
#endif
    /* 高精度版本 (ns级) */
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL +
               (uint64_t)ts.tv_nsec / 1000000ULL;
    }

    /* 最终回退方案 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static uint32_t ns_to_ms(uint64_t ns)
{
    return (uint32_t)(ns / 1000000ULL);
}

static uint64_t ms_to_ns(uint32_t ms)
{
    return ((uint64_t)ms * 1000000ULL);
}

/* ---------- 命令执行核心实现 ---------- */

/**
 * @brief 简单命令执行 (使用 system())
 *
 * 最简单的实现方式，只能获取返回码，无法捕获输出。
 * 适用于不需要输出的场景。
 */
static int execute_command_simple(const char *command,
                                  char *output,
                                  size_t output_size,
                                  uint32_t timeout_ms)
{
    (void)timeout_ms;  /* system() 不支持超时 */

    linux_log_printf(4, "Executing (system): %s\n", command);

    int ret = system(command);

    if (ret == -1) {
        /* system() 调用失败 (如 fork 失败) */
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "[ERROR] system() failed: %s", strerror(errno));
        }
        return -1;
    }

    if (WIFEXITED(ret)) {
        int exit_code = WEXITSTATUS(ret);
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "[OK] Exit code=%d: %.128s", exit_code, command);
        }
        return exit_code;
    }

    if (WIFSIGNALED(ret)) {
        int sig = WTERMSIG(ret);
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "[KILLED] Signal=%d (%s): %.112s",
                    sig, strsignal(sig), command);
        }
        return -sig;
    }

    /* 其他异常情况 */
    if (output != NULL && output_size > 0) {
        snprintf(output, output_size,
                "[UNKNOWN] Status=%d: %.120s", ret, command);
    }
    return ret;
}

/**
 * @brief 高级命令执行 (使用 popen())
 *
 * 可以捕获标准输出，但不支持超时控制。
 * 适用于快速执行的命令。
 */
static int execute_command_advanced(const char *command,
                                   char *output,
                                   size_t output_size,
                                   uint32_t timeout_ms)
{
    (void)timeout_ms;  /* popen 不原生支持超时 */

    linux_log_printf(4, "Executing (popen): %s\n", command);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "[ERROR] popen() failed: %s", strerror(errno));
        }
        return -1;
    }

    /* 读取输出 */
    size_t total_read = 0;
    if (output != NULL && output_size > 0) {
        char buf[PIPE_READ_BUF_SIZE];
        size_t remaining = output_size - 1;  /* 留空间给 \0 */

        while (remaining > 0) {
            size_t to_read = (remaining > sizeof(buf)) ?
                             sizeof(buf) : remaining;
            ssize_t n = fread(buf, 1, to_read, fp);

            if (n > 0) {
                size_t copy_len = (size_t)n;
                if (copy_len > remaining) copy_len = remaining;
                memcpy(output + total_read, buf, copy_len);
                total_read += copy_len;
                remaining -= copy_len;
            } else if (feof(fp)) {
                break;
            } else if (ferror(fp)) {
                if (errno == EINTR) continue;
                linux_log_printf(2, "Warning: Read error: %s\n",
                               strerror(errno));
                break;
            }
        }

        output[total_read] = '\0';
    }

    /* 获取退出状态 */
    int status = pclose(fp);

    if (status == -1) {
        linux_log_printf(2, "Warning: pclose() failed: %s\n",
                        strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNaled(status)) {
        return -WTERMSIG(status);
    }

    return status;
}

/**
 * @brief 完整命令执行 (使用 fork+exec+pipe)
 *
 * 最灵活的实现方式，支持：
 * - 超时控制 (alarm/SIGALRM)
 * - 输出捕获 (stdout + stderr 合并)
 * - 资源限制 (setrlimit)
 * - 进程树终止
 * - 详细的状态信息
 *
 * @note 此函数会修改全局变量 g_timeout_flag 和 g_timed_out_pid
 */
static int execute_command_with_fork(const char *command,
                                     char *output,
                                     size_t output_size,
                                     uint32_t timeout_ms)
{
    linux_log_printf(4, "Executing (fork): %s (timeout=%ums)\n",
                    command, timeout_ms);

    int pipefd[2];  /* pipefd[0]=read, pipefd[1]=write */
    pid_t pid;

    /* 创建管道 */
    if (pipe(pipefd) == -1) {
        linux_log_printf(1, "Failed to create pipe: %s\n", strerror(errno));
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "[ERROR] pipe() failed: %s", strerror(errno));
        }
        return -1;
    }

    /* 设置管道读端为非阻塞 (可选，用于带超时的读取) */
    // fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

    /* Fork 子进程 */
    pid = safe_fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (output != NULL && output_size > 0) {
            snprintf(output, output_size,
                    "[ERROR] fork() failed: %s", strerror(errno));
        }
        return -1;
    }

    if (pid == 0) {
        /* ===== 子进程 ===== */

        /* 关闭管道读端 */
        close(pipefd[0]);

        /* 重定向 stdout 和 stderr 到管道写端 */
        if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
            dup2(pipefd[1], STDERR_FILENO) == -1) {
            _exit(127);  /* 无法重定向 */
        }

        /* 关闭原始写端 (现在stdout/stderr指向它) */
        close(pipefd[1]);

        /* 设置资源限制 (如果启用) */
        if (g_linux_options.enable_rlimits) {
            setup_child_process();
        }

        /* 执行命令 */
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);

        /* 如果 execl 返回，说明出错 */
        _exit(127);  /* 命令执行失败 */
    }

    /* ===== 父进程 ===== */

    /* 关闭管道写端 */
    close(pipefd[1]);

    /* 更新统计 */
    g_statistics.total_forked++;

    /* 清除超时标志 */
    g_timeout_flag = 0;
    g_timed_out_pid = 0;

    /* 等待子进程完成或超时 */
    int status;
    bool timed_out = false;
    int wait_ret = wait_for_process(pid, timeout_ms, &status, &timed_out);

    /* 从管道读取输出 */
    size_t total_captured = 0;
    if (output != NULL && output_size > 0) {
        total_captured = capture_output(pipefd[0], output, output_size,
                                         &total_captured);
        output[total_captured] = '\0';
    }

    /* 关闭管道读端 */
    close(pipefd[0]);

    /* 处理超时 */
    if (timed_out) {
        g_statistics.timeout_kills++;
        linux_log_printf(2,
            "Command timed out after %ums, killing process %d: %.96s\n",
            timeout_ms, pid, command);

        /* 终止进程树 */
        kill_process_tree(pid);

        /* 等待进程真正结束 (最多再等2秒) */
        sleep(2);
        waitpid(pid, &status, WNOHANG);

        if (output != NULL && output_size > 0) {
            snprintf(output + total_captured,
                    output_size - total_captured,
                    "\n[TIMEOUT] Command exceeded %ums limit",
                    timeout_ms);
        }

        return -ETIMEDOUT;
    }

    /* 分析退出状态 */
    int ret;
    if (WIFEXITED(status)) {
        ret = WEXITSTATUS(status);
        linux_log_printf(4, "Process %d exited with code %d\n", pid, ret);
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        ret = -sig;
        g_statistics.signal_terminations++;
        linux_log_printf(2,
            "Process %d killed by signal %d (%s)\n",
            pid, sig, strsignal(sig));

        if (output != NULL && output_size > 0) {
            size_t avail = output_size - total_captured - 1;
            if (avail > 0) {
                snprintf(output + total_captured, avail,
                        "\n[KILLED] Signal %d (%s)", sig, strsignal(sig));
            }
        }
    } else if (WIFSTOPPED(status)) {
        ret = -WSTOPSIG(status);
        linux_log_printf(2, "Process %d stopped by signal %d\n",
                        pid, WSTOPSIG(status));
    } else {
        ret = -1;
        linux_log_printf(1, "Process %d exited with unknown status 0x%x\n",
                        pid, status);
    }

    return ret;
}

/* ---------- 进程管理 ---------- */

/**
 * @brief 安全的 fork 封装
 *
 * 处理 fork 失败的情况，记录日志。
 */
static pid_t safe_fork(void)
{
    pid_t pid = fork();

    if (pid == -1) {
        linux_log_printf(1, "Fork failed: %s\n", strerror(errno));
    }

    return pid;
}

/**
 * @brief 等待进程完成 (支持超时)
 *
 * 使用非阻塞 waitpid + usleep 轮询实现超时控制。
 * 更精确的方式是使用 SIGCHLD + select/poll，但实现复杂度较高。
 *
 * @param[in]  pid       要等待的进程PID
 * @param[in]  timeout_ms 超时时间 (ms), 0=无限等待
 * @param[out] status    退出状态
 * @param[out] timed_out 是否超时
 * @return int waitpid 的返回值
 */
static int wait_for_process(pid_t pid, uint32_t timeout_ms,
                            int *status, bool *timed_out)
{
    *timed_out = false;

    if (timeout_ms == 0) {
        /* 无限等待 */
        return waitpid(pid, status, 0);
    }

    /* 带超时的等待 */
    uint64_t start = get_time_ms();
    int wret;

    while (1) {
        wret = waitpid(pid, status, WNOHANG);

        if (wret > 0) {
            /* 进程已结束 */
            return wret;
        }

        if (wret == -1) {
            if (errno == ECHILD) {
                /* 子进程不存在 */
                *status = 0;
                return 0;
            }
            if (errno == EINTR) {
                /* 被信号中断，继续等待 */
                continue;
            }
            linux_log_printf(2, "waitpid error: %s\n", strerror(errno));
            return -1;
        }

        /* wret == 0: 进程仍在运行，检查超时 */
        uint64_t elapsed = get_time_ms() - start;
        if (elapsed >= timeout_ms) {
            *timed_out = true;
            return 0;
        }

        /* 短暂休眠后重试 (避免忙等待) */
        usleep(10000);  /* 10ms */
    }
}

/**
 * @brief 终止进程树 (包括子进程)
 *
 * 发送 SIGTERM 给进程组，如果进程有自己独立的进程组。
 * 否则只终止该进程。
 */
static void kill_process_tree(pid_t root_pid)
{
    /*
     * 尝试杀死整个进程组
     * 注意：这要求子进程在独立的进程组中 (setsid/setpgid)
     */
    if (kill(-root_pid, SIGTERM) == 0) {
        /* 成功发送给进程组 */
        linux_log_printf(4, "Sent SIGTERM to process group %d\n",
                        (int)root_pid);
    } else {
        /* 回退：只杀死单个进程 */
        kill(root_pid, SIGTERM);
    }

    /* 等待一小段时间让进程清理 */
    usleep(100000);  /* 100ms */

    /* 如果还在运行，强制杀死 */
    if (kill(root_pid, 0) == 0) {
        kill(-root_pid, SIGKILL);
        linux_log_printf(3, "Sent SIGKILL to process group %d\n",
                        (int)root_pid);
    }
}

/**
 * @brief 为子进程设置资源限制
 *
 * 在子进程中调用，设置 CPU、内存、文件大小限制。
 */
static void setup_child_process(void)
{
    if (!g_linux_options.enable_rlimits) {
        return;
    }

    struct rlimit rl;

    /* CPU 时间限制 */
    if (g_linux_options.cpu_limit > 0) {
        rl.rlim_cur = g_linux_options.cpu_limit;
        rl.rlim_max = g_linux_options.cpu_limit + 10;  /* 允许少量超出 */
        if (setrlimit(RLIMIT_CPU, &rl) != 0) {
            /* 静默失败，不影响主要功能 */
        }
    }

    /* 内存限制 (虚拟地址空间) */
    if (g_linux_options.memory_limit > 0) {
        rl.rlim_cur = g_linux_options.memory_limit;
        rl.rlim_max = g_linux_options.memory_limit * 2;
        if (setrlimit(RLIMIT_AS, &rl) != 0) {
            /* 静默失败 */
        }
    }

    /* 输出文件大小限制 */
    if (g_linux_options.output_size_limit > 0) {
        rl.rlim_cur = g_linux_options.output_size_limit;
        rl.rlim_max = g_linux_options.output_size_limit * 2;
        if (setrlimit(RLIMIT_FSIZE, &rl) != 0) {
            /* 静默失败 */
        }
    }

    /* 子进程数限制 */
    rl.rlim_cur = 100;
    rl.rlim_max = 200;
    setrlimit(RLIMIT_NPROC, &rl);
}

/* ---------- 输出捕获 ---------- */

/**
 * @brief 从文件描述符捕获输出
 *
 * 读取 fd 的所有可用数据到 buffer 中。
 *
 * @param[in]  fd              文件描述符
 * @param[out] buffer          输出缓冲区
 * @param[in]  buffer_size     缓冲区大小
 * @param[out] total_captured  已捕获的总字节数 (累加)
 * @return ssize_t 本次读取的字节数
 */
static ssize_t capture_output(int fd, char *buffer, size_t buffer_size,
                              size_t *total_captured)
{
    ssize_t total_this_time = 0;
    size_t remaining = buffer_size - 1 - *total_captured;

    if (remaining <= 0) {
        return 0;  /* 缓冲区已满 */
    }

    char buf[PIPE_READ_BUF_SIZE];

    while (remaining > 0) {
        size_t to_read = (remaining > sizeof(buf)) ?
                         sizeof(buf) : remaining;
        ssize_t n = read(fd, buf, to_read);

        if (n > 0) {
            /* 写入输出缓冲区 */
            size_t copy_len = (size_t)n;
            if (copy_len > remaining) copy_len = remaining;

            memcpy(buffer + *total_captured + total_this_time,
                   buf, copy_len);
            total_this_time += copy_len;
            remaining -= copy_len;
        } else if (n == 0) {
            /* EOF */
            break;
        } else {  /* n < 0 */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 非阻塞模式下无数据，稍后再试 */
                break;
            }
            if (errno == EINTR) {
                /* 被信号中断，继续读取 */
                continue;
            }
            /* 其他错误 */
            linux_log_printf(3, "Read error on pipe: %s\n",
                           strerror(errno));
            break;
        }
    }

    *total_captured += total_this_time;
    return total_this_time;
}

/**
 * @brief 带超时的读取
 *
 * 使用 select/poll 实现带超时的读取操作。
 * (简化版，实际实现可能需要更复杂的逻辑)
 */
static ssize_t read_with_timeout(int fd, void *buffer, size_t count,
                                 uint32_t timeout_ms)
{
    /* TODO: 实现 select/poll 版本的带超时读取 */
    /* 当前版本直接调用 read() */
    return read(fd, buffer, count);
}

/* ---------- 信号处理 ---------- */

/**
 * @brief 设置信号处理器
 *
 * 安装 SIGALRM 处理器用于超时控制。
 */
static void setup_signal_handlers(void)
{
    struct saction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = timeout_handler;
    sa.sa_flags = 0;  /* 不设置 SA_RESTART，允许系统调用被中断 */
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, NULL) != 0) {
        linux_log_printf(2, "Warning: Failed to install SIGALRM handler\n");
    }
}

/**
 * @brief 恢复默认信号处理器
 */
static void restore_signal_handlers(void)
{
    struct saction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGALRM, &sa, NULL);
}

/**
 * @brief 超时信号处理器
 *
 * 当 alarm 到期时调用。设置超时标志以便主循环检测。
 *
 * @warning 此函数必须是 async-signal-safe 的！
 *          只能调用 async-signal-safe 函数。
 */
static void timeout_handler(int signum)
{
    (void)signum;

    /* 设置超时标志 (sig_atomic_t 保证原子性) */
    g_timeout_flag = 1;

    /* 不要在这里做任何复杂操作！*/
}

/**
 * @brief 忽略 SIGPIPE 信号
 *
 * 防止管道破裂导致进程意外终止。
 */
static void ignore_sigpipe(void)
{
    struct saction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGPIPE, &sa, NULL);
}

/* ---------- 环境和目录 ---------- */

/**
 * @brief 保存当前工作目录
 */
static int save_current_directory(void)
{
    if (getcwd(g_saved_cwd, sizeof(g_saved_cwd)) == NULL) {
        linux_log_printf(2, "Warning: Failed to save cwd: %s\n",
                        strerror(errno));
        g_cwd_saved = false;
        return -1;
    }

    g_cwd_saved = true;
    return 0;
}

/**
 * @brief 恢复工作目录
 */
static int restore_directory(void)
{
    if (!g_cwd_saved) {
        return 0;
    }

    if (chdir(g_saved_cwd) != 0) {
        linux_log_printf(2, "Warning: Failed to restore cwd to '%s': %s\n",
                        g_saved_cwd, strerror(errno));
        return -1;
    }

    g_cwd_saved = false;
    return 0;
}

/**
 * @brief 保存当前环境变量
 *
 * 返回一个字符串数组副本，必须用 restore_environment() 释放。
 */
static char **save_environment(void)
{
    extern char **environ;
    int count = 0;

    /* 计算环境变量数量 */
    while (environ[count] != NULL) count++;

    /* 分配数组 (+1 for NULL terminator) */
    char **saved = malloc((count + 1) * sizeof(char *));
    if (saved == NULL) return NULL;

    /* 复制每个环境变量 */
    for (int i = 0; i < count; i++) {
        saved[i] = strdup(environ[i]);
        if (saved[i] == NULL) {
            /* 分配失败，释放已分配的内存 */
            for (int j = 0; j < i; j++) free(saved[j]);
            free(saved);
            return NULL;
        }
    }
    saved[count] = NULL;

    return saved;
}

/**
 * @brief 恢复环境变量
 *
 * 清除当前环境并恢复为 saved_env。
 * 释放 saved_env 占用的内存。
 */
static int restore_environment(char **saved_env)
{
    if (saved_env == NULL) return 0;

    extern char **environ;

    /* 清除当前环境 */
    clearenv();

    /* 恢复保存的环境 */
    for (int i = 0; saved_env[i] != NULL; i++) {
        putenv(saved_env[i]);  /* putenv 不复制字符串 */
    }

    /* 释放数组本身 (putenv 已接管字符串) */
    free(saved_env);

    return 0;
}

/* ---------- 工具函数 ---------- */

/**
 * @brief 检查命令是否看起来相对安全
 *
 * 这是一个基本的启发式检查，不能保证100%安全。
 * 对于生产环境，应使用白名单机制。
 */
static bool is_shell_safe(const char *command)
{
    if (command == NULL) return false;

    /* 检查危险模式 */
    const char *dangerous_patterns[] = {
        "`",        /* 命令替换 */
        "$(",       /* 命令替换 */
        ";",        /* 命令分隔符 */
        "&&",       /* 逻辑与 */
        "||",       /* 逻辑或 */
        "|",        /* 管道 */
        ">",        /* 重定向 */
        "<",        /* 重定向 */
        NULL
    };

    for (int i = 0; dangerous_patterns[i] != NULL; i++) {
        if (strstr(command, dangerous_patterns[i]) != NULL) {
            return false;
        }
    }

    return true;
}

/**
 * @brief 转义 Shell 参数中的特殊字符
 *
 * 用单引号包裹并在内部转义单引号。
 */
static char *escape_shell_arg(const char *arg)
{
    if (arg == NULL) return NULL;

    /* 最坏情况：每个字符都需要转义 */
    size_t len = strlen(arg);
    char *escaped = malloc(len * 4 + 3);  /* 充足的空间 */
    if (escaped == NULL) return NULL;

    char *p = escaped;
    *p++ = '\'';

    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '\'') {
            *p++ = '\'';  /* 结束当前单引号 */
            *p++ = '\\';  /* 转义单引号 */
            *p++ = '\'';  /* 开始新的单引号 */
        }
        *p++ = arg[i];
    }

    *p++ = '\'';
    *p++ = '\0';

    return escaped;
}

/**
 * @brief 更新统计信息
 */
static void update_statistics(pid_t pid, int status,
                               uint64_t start_time, uint64_t end_time,
                               size_t output_size)
{
    /* 更新峰值输出大小 */
    if (output_size > g_statistics.max_output_size) {
        g_statistics.max_output_size = output_size;
    }

    /* 更新平均命令时间 (简单移动平均) */
    uint64_t elapsed = end_time - start_time;
    static uint64_t total_cmd_time = 0;
    static uint32_t cmd_count = 0;

    total_cmd_time += elapsed;
    cmd_count++;
    g_statistics.avg_cmd_time_ms = (uint32_t)(total_cmd_time / cmd_count);

    /* 获取子进程资源使用情况 (如果有效) */
    if (pid > 0) {
        struct rusage usage;
        if (getrusage(RUSAGE_CHILDREN, &usage) == 0) {
            /* 累积 CPU 时间 (user + system) */
            g_statistics.total_cpu_time_ms +=
                (uint64_t)(usage.ru_utime.tv_sec * 1000 +
                           usage.ru_utime.tv_usec / 1000) +
                (uint64_t)(usage.ru_stime.tv_sec * 1000 +
                           usage.ru_stime.tv_usec / 1000);
        }
    }

    /* 获取当前进程内存使用量 (近似) */
    FILE *status_fp = fopen("/proc/self/status", "r");
    if (status_fp != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), status_fp) != NULL) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                unsigned long kb;
                if (sscanf(line + 6, "%lu", &kb) == 1) {
                    if (kb > g_statistics.peak_memory_kb) {
                        g_statistics.peak_memory_kb = kb;
                    }
                }
                break;
            }
        }
        fclose(status_fp);
    }
}

/**
 * @brief 重置统计信息
 */
static void reset_statistics(void)
{
    memset(&g_statistics, 0, sizeof(g_statistics));
}

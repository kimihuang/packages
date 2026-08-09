/**
 * @file rtthread_adapter.h
 * @brief RT-Thread 平台适配层头文件
 *
 * 本文件定义了 RT-Thread 操作系统的平台适配接口，
 * 将 RT-Thread 的底层能力映射到自动运行框架的标准平台操作表。
 *
 * RT-Thread 环境特点:
 * 1. **运行级别**: Thread mode (特权级或用户级，取决于MPU配置)
 * 2. **内存环境**: DDR (MMU可选开启)
 * 3. **Shell能力**: 完整的 finsh/msh 命令行交互
 * 4. **调试输出**: 通过 UART (使用 rt_kprintf / LOG_x 宏)
 * 5. **时间源**: 系统时钟滴答 (rt_tick_get) 或 HWTIMER 设备
 * 6. **并发模型**: 多线程/多任务调度，支持信号量/互斥锁
 * 7. **设备模型**: 统一的设备驱动框架 (device driver framework)
 * 8. **Shell命令**: 支持 finsh/msh 命令执行和输出捕获
 *
 * 与BL1/BL31的关键差异:
 * - 内存充足 (DDR vs SRAM)
 * - 支持完整的 Shell 命令执行
 * - 多任务环境，需要注意线程安全
 * - 拥有丰富的设备驱动和文件系统支持
 * - 可使用动态内存分配 (RT_USING_HEAP)
 *
 * 适配策略:
 * - 日志: 使用 RT-Thread 的 rt_kprintf() 或 LOG_I/LOG_E/LOG_W 宏
 * - 命令执行: 调用 finsh/msh 命令执行接口 (rt_exec_cmd 或自定义实现)
 * - 时间戳: 使用 rt_tick_get() 转换为毫秒
 * - 延时: 使用 rt_thread_mdelay() 或 rt_thread_delay()
 * - 内存: 使用标准 memset/memcpy 或 rt_memcpy/rt_memset
 *
 * 典型集成方式:
 * @code
 *   // 在 main() 函数或其他初始化位置
 *   #include "adapter/rtthread_adapter.h"
 *
 *   int main(void) {
 *       // ... 原有初始化代码 ...
 *
 *       // 执行自动运行任务
 *       int ret = rtthread_run_autorun();
 *
 *       // ... 继续原有流程 ...
 *   }
 * @endcode
 *
 * 配置要求:
 * - 配置数据地址: DDR中的固定地址 (PLAT_RTTHREAD_AUTORUN_CONFIG_ADDR)
 * - 结果区域地址: DDR (PLAT_RTTHREAD_AUTORUN_RESULT_ADDR)
 * - 最大配置大小: 建议 < 32KB (DDR空间充足)
 *
 * @note RT-Thread阶段资源相对充足，可以:
 *       1. 执行各种测试任务 (外设驱动、网络、文件系统等)
 *       2. 任务数量可较多 (建议 < 50个)
 *       3. 启用详细日志便于调试
 *       4. 使用完整的正则表达式匹配
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef RTTHREAD_ADAPTER_H
#define RTTHREAD_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../core/autorun_config.h"

/* ================================================================== */
/*                    平台特定配置                                    */
/* ================================================================== */

/**
 * @brief RT-Thread 阶段自动运行配置的物理地址
 *
 * 此地址应由PC侧烧录工具或Bootloader写入。
 * 默认值可根据具体SoC平台修改。
 * 建议放在DDR中不会被覆盖的区域。
 */
#ifndef PLAT_RTTHREAD_AUTORUN_CONFIG_ADDR
#define PLAT_RTTHREAD_AUTORUN_CONFIG_ADDR    0x20000000UL
#endif

/**
 * @brief RT-Thread 阶段结果输出的物理地址
 */
#ifndef PLAT_RTTHREAD_AUTORUN_RESULT_ADDR
#define PLAT_RTTHREAD_AUTORUN_RESULT_ADDR     0x20040000UL
#endif

/**
 * @brief RT-Thread 阶段结果区域最大大小 (bytes)
 *
 * RT-Thread阶段DDR空间充足，可设置较大的结果区域以记录详细信息。
 */
#ifndef PLAT_RTTHREAD_AUTORUN_RESULT_SIZE
#define PLAT_RTTHREAD_AUTORUN_RESULT_SIZE     32768
#endif

/**
 * @brief RT-Thread 系统时钟频率 (Hz)
 *
 * 用于将 tick 转换为毫秒。
 * 默认值1000表示1ms一个tick（常见配置）。
 */
#ifndef RTTHREAD_TICK_PER_SECOND
#define RTTHREAD_TICK_PER_SECOND             1000UL
#endif

/* ================================================================== */
/*                   核心API函数声明                                  */
/* ================================================================== */

/**
 * @brief 初始化并运行RT-Thread阶段的自动任务
 *
 * 这是RT-Thread适配层的入口函数，完成以下工作:
 * 1. 准备平台操作函数表 (映射到RT-Thread API)
 * 2. 调用 autorun_init() 初始化框架
 * 3. 调用 autorun_run_stage("rtthread") 执行所有RT-Thread任务
 * 4. 输出简要结果摘要到控制台
 *
 * @return int
 *         0 = 所有任务通过
 *         非0 = 有失败任务或初始化错误
 */
int rtthread_run_autorun(void);

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * @param[in] config_addr 配置数据的物理地址
 * @param[in] result_addr 结果区域的物理地址
 * @param[in] result_size 结果区域大小 (bytes)
 *
 * @return int 0=成功, 非0=错误
 */
int rtthread_run_autorun_with_addr(uint32_t config_addr,
                                   uint32_t result_addr,
                                   uint32_t result_size);

/**
 * @brief 获取RT-Thread适配器的平台操作表
 *
 * @return const autorun_platform_ops_t* 平台操作表指针 (静态存储)
 */
const autorun_platform_ops_t* rtthread_get_platform_ops(void);

/* ================================================================== */
/*                    状态查询                                       */
/* ================================================================== */

/**
 * @brief 检查RT-Thread自动运行是否已完成
 *
 * @return bool true=已完成, false=未执行或正在执行
 */
bool rtthread_is_autorun_completed(void);

/**
 * @brief 获取最后一次执行的返回码
 *
 * @return int 最后一次 rtthread_run_autorun() 的返回值
 */
int rtthread_get_last_result(void);

#ifdef __cplusplus
}
#endif

#endif /* RTTHREAD_ADAPTER_H */

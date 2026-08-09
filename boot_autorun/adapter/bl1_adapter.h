/**
 * @file bl1_adapter.h
 * @brief ATF BL1 阶段平台适配层头文件
 *
 * 本文件定义了ARM Trusted Firmware (ATF) BL1阶段的平台适配接口，
 * 将ATF的底层能力映射到自动运行框架的标准平台操作表。
 *
 * ATF BL1 环境特点:
 * 1. **运行级别**: EL3 (Exception Level 3, 最高特权级)
 * 2. **内存环境**: SRAM + 少量DDR (MMU已开启)
 * 3. **Shell能力**: 极其有限，通常无交互式Shell
 * 4. **调试输出**: 通过UART (使用 tf_log / INFO / ERROR 等宏)
 * 5. **时间源**: 通用定时器 (Generic Timer) 或系统计数器
 * 6. **并发模型**: 单CPU (主核) 执行，无多线程/中断复杂度
 *
 * 适配策略:
 * - 日志: 使用ATF的 tf_log() / INFO() / WARN() / ERROR() 宏
 * - 命令执行: 调用平台已实现的命令执行接口
 * - 时间戳: 使用 ARM Generic Timer (CNTVCT_EL0 或 CNTPCT_EL0)
 * - 延时: 忙等待循环 (基于 Generic Timer)
 * - 内存: 使用 ATF 提供的 memset/memcpy (来自 lib/libc)
 *
 * 典型集成方式:
 * @code
 *   // 在 bl1_main.c 的 bl1_main() 函数中
 *   #include "adapter/bl1_adapter.h"
 *
 *   void bl1_main(void) {
 *       // ... 原有初始化代码 ...
 *
 *       // 执行自动运行任务
 *       int ret = bl1_run_autorun();
 *
 *       // ... 继续原有流程 ...
 *   }
 * @endcode
 *
 * 配置要求:
 * - 配置数据地址: 通常在SRAM顶部或由平台定义 (PLAT_AUTORUN_CONFIG_ADDR)
 * - 结果区域地址: SRAM或Secure DDR (PLAT_AUTORUN_RESULT_ADDR)
 * - 最大配置大小: 建议 < 8KB (受限于SRAM空间)
 *
 * @note BL1阶段资源极其有限，建议:
 *       1. 只执行关键性测试任务 (如DDR初始化验证)
 *       2. 任务数量控制在5个以内
 *       3. 禁用详细日志以减少UART输出开销
 *       4. 考虑使用计算法CRC32而非查表法 (节省1KB)
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef BL1_ADAPTER_H
#define BL1_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../core/autorun_config.h"

/* ================================================================== */
/*                    平台特定配置                                    */
/* ================================================================== */

/**
 * @brief BL1阶段自动运行配置的物理地址
 *
 * 此地址应由PC侧烧录工具或Boot ROM写入。
 * 默认值可根据具体SoC平台修改。
 */
#ifndef PLAT_BL1_AUTORUN_CONFIG_ADDR
#define PLAT_BL1_AUTORUN_CONFIG_ADDR    0xFFFF0000UL
#endif

/**
 * @brief BL1阶段结果输出的物理地址
 */
#ifndef PLAT_BL1_AUTORUN_RESULT_ADDR
#define PLAT_BL1_AUTORUN_RESULT_ADDR     0xFFFF8000UL
#endif

/**
 * @brief BL1阶段结果区域最大大小 (bytes)
 */
#ifndef PLAT_BL1_AUTORUN_RESULT_SIZE
#define PLAT_BL1_AUTORUN_RESULT_SIZE     4096
#endif

/* ================================================================== */
/*                   核心API函数声明                                  */
/* ================================================================== */

/**
 * @brief 初始化并运行BL1阶段的自动任务
 *
 * 这是BL1适配层的入口函数，完成以下工作:
 * 1. 准备平台操作函数表 (映射到ATF API)
 * 2. 调用 autorun_init() 初始化框架
 * 3. 调用 autorun_run_stage("bl1") 执行所有BL1任务
 * 4. 输出简要结果摘要到控制台
 *
 * @return int
 *         0 = 所有任务通过
 *         非0 = 有失败任务或初始化错误
 */
int bl1_run_autorun(void);

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * @param[in] config_addr 配置数据的物理地址
 * @param[in] result_addr 结果区域的物理地址
 * @param[in] result_size 结果区域大小 (bytes)
 *
 * @return int 0=成功, 非0=错误
 */
int bl1_run_autorun_with_addr(uint32_t config_addr,
                              uint32_t result_addr,
                              uint32_t result_size);

/**
 * @brief 获取BL1适配器的平台操作表
 *
 * @return const autorun_platform_ops_t* 平台操作表指针 (静态存储)
 */
const autorun_platform_ops_t* bl1_get_platform_ops(void);

/* ================================================================== */
/*                    状态查询                                       */
/* ================================================================== */

/**
 * @brief 检查BL1自动运行是否已完成
 *
 * @return bool true=已完成, false=未执行或正在执行
 */
bool bl1_is_autorun_completed(void);

/**
 * @brief 获取最后一次执行的返回码
 *
 * @return int 最后一次 bl1_run_autorun() 的返回值
 */
int bl1_get_last_result(void);

#ifdef __cplusplus
}
#endif

#endif /* BL1_ADAPTER_H */

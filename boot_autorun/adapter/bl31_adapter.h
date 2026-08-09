/**
 * @file bl31_adapter.h
 * @brief ATF BL31 阶段平台适配层头文件
 *
 * 本文件定义了ARM Trusted Firmware (ATF) BL31阶段的平台适配接口，
 * 将ATF EL3运行环境的能力映射到自动运行框架的标准平台操作表。
 *
 * ATF BL31 环境特点:
 * 1. **运行级别**: EL3 (Exception Level 3, 最高特权级)
 * 2. **内存环境**: DDR (Secure/Normal World) + MMU已完全配置
 * 3. **Shell能力**: 无交互式Shell (但可通过SMC调用执行操作)
 * 4. **调试输出**: 通过UART (使用 tf_log / INFO / ERROR 等宏)
 * 5. **时间源**: ARM Generic Timer (CNTVCT_EL0 或 CNTPCT_EL0)
 * 6. **并发模型**: 单CPU初始化阶段，后续可多核
 * 7. **中断支持**: 完整的中断管理 (GICv2/GICv3)
 * 8. **SMC接口**: 可接收来自Secure Monitor/Normal World的调用
 *
 * 与BL1的关键差异:
 * - 内存更充足 (DDR vs SRAM)
 * - 支持完整的中断和异常处理
 * - 可通过SMC与Lower ELs通信
 * - PSCI已初始化，可进行电源管理
 * - TrustZone配置已完成
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef BL31_ADAPTER_H
#define BL31_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../core/autorun_config.h"

/* ================================================================== */
/*                    平台特定配置                                    */
/* ================================================================== */

#ifndef PLAT_BL31_AUTORUN_CONFIG_ADDR
#define PLAT_BL31_AUTORUN_CONFIG_ADDR    0x10000000UL
#endif

#ifndef PLAT_BL31_AUTORUN_RESULT_ADDR
#define PLAT_BL31_AUTORUN_RESULT_ADDR     0x10020000UL
#endif

#ifndef PLAT_BL31_AUTORUN_RESULT_SIZE
#define PLAT_BL31_AUTORUN_RESULT_SIZE     16384
#endif

/* ================================================================== */
/*                   核心API函数声明                                  */
/* ================================================================== */

/**
 * @brief 初始化并运行BL31阶段的自动任务
 *
 * @return int 0=所有任务通过, 非0=有失败或错误
 */
int bl31_run_autorun(void);

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * @param[in] config_addr 配置数据的物理地址
 * @param[in] result_addr 结果区域的物理地址
 * @param[in] result_size 结果区域大小 (bytes)
 *
 * @return int 0=成功, 非0=错误
 */
int bl31_run_autorun_with_addr(uint32_t config_addr,
                               uint32_t result_addr,
                               uint32_t result_size);

/**
 * @brief 获取BL31适配器的平台操作表
 *
 * @return const autorun_platform_ops_t* 平台操作表指针
 */
const autorun_platform_ops_t* bl31_get_platform_ops(void);

/* ================================================================== */
/*                    状态查询                                       */
/* ================================================================== */

bool bl31_is_autorun_completed(void);
int bl31_get_last_result(void);

#ifdef __cplusplus
}
#endif

#endif /* BL31_ADAPTER_H */

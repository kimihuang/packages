/**
 * @file uboot_adapter.h
 * @brief U-Boot 平台适配层头文件
 *
 * 本文件定义了 U-Boot (Das U-Boot) 引导加载程序的平台适配接口，
 * 将 U-Boot 提供的底层能力映射到自动运行框架的标准平台操作表。
 *
 * U-Boot 环境特点:
 * 1. **运行级别**: EL2/EL1 (ARM64) 或 S/U模式 (RISC-V), 特权级
 * 2. **内存环境**: DDR (MMU可选开启, 取决于CONFIG_MMU)
 * 3. **Shell能力**: 完整的 U-Boot 命令行交互 (run_command)
 * 4. **调试输出**: 通过 UART (使用 printf / puts / debug())
 * 5. **时间源**: 系统定时器 (timer_get_us / get_timer)
 * 6. **并发模型**: 单线程执行 (无多任务调度)
 * 7. **设备模型**: Driver Model (DM) 统一设备驱动框架
 * 8. **环境变量**: 支持丰富的环境变量系统 (env_get/env_set)
 *
 * 与BL1/BL31的关键差异:
 * - 内存充足 (DDR vs SRAM)
 * - 支持完整的 Shell 命令执行和输出捕获
 * - 拥有 Driver Model 设备驱动框架
 * - 支持文件系统操作 (FAT/EXT4/SquashFS等)
 * - 可使用动态内存分配 (malloc/free)
 * - 拥有网络协议栈 (TCP/IP, DHCP, TFTP等)
 *
 * 与RT-Thread的关键差异:
 * - 单线程环境 (无多任务)
 * - 无操作系统调度器
 * - 命令执行更直接 (run_command API)
 * - 主要用于引导阶段测试
 *
 * 适配策略:
 * - 日志: 使用 U-Boot 的 printf() / debug() 宏
 * - 命令执行: 调用 run_command() 或 run_command_list()
 * - 时间戳: 使用 timer_get_us() 转换为毫秒
 * - 延时: 使用 mdelay() 或 udelay()
 * - 内存: 使用标准 memset/memcpy 或 U-Boot 的 memmove/memset
 *
 * 典型集成方式:
 * @code
 *   // 在 board_init_r() 函数或自定义命令中
 *   #include "adapter/uboot_adapter.h"
 *
 *   int do_autorun(struct cmd_tbl *cmdtp, int flag,
 *                  int argc, char *const argv[]) {
 *       // 执行自动运行任务
 *       return uboot_run_autorun();
 *   }
 *
 *   U_BOOT_CMD(autorun, 1, 0, do_autorun,
 *              "Run auto test tasks", "Run all configured autorun tasks");
 * @endcode
 *
 * 配置要求:
 * - 配置数据地址: DDR中的固定地址 (PLAT_UBOOT_AUTORUN_CONFIG_ADDR)
 * - 结果区域地址: DDR (PLAT_UBOOT_AUTORUN_RESULT_ADDR)
 * - 最大配置大小: 建议 < 32KB (DDR空间充足)
 *
 * @note U-Boot阶段资源相对充足，可以:
 *       1. 执行各种硬件初始化验证任务
 *       2. 测试外设驱动 (存储、网络、显示等)
 *       3. 验证设备树配置和环境变量
 *       4. 启用详细日志便于调试
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef UBOOT_ADAPTER_H
#define UBOOT_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../core/autorun_config.h"

/* ================================================================== */
/*                    平台特定配置                                    */
/* ================================================================== */

/**
 * @brief U-Boot 阶段自动运行配置的物理地址
 *
 * 此地址应由PC侧烧录工具或上一级Bootloader写入。
 * 默认值可根据具体SoC平台修改。
 * 建议放在DDR中不会被U-Boot重定位覆盖的区域，
 * 或在board_init_f之后确定的安全区域。
 */
#ifndef PLAT_UBOOT_AUTORUN_CONFIG_ADDR
#define PLAT_UBOOT_AUTORUN_CONFIG_ADDR    0x30000000UL
#endif

/**
 * @brief U-Boot 阶段结果输出的物理地址
 */
#ifndef PLAT_UBOOT_AUTORUN_RESULT_ADDR
#define PLAT_UBOOT_AUTORUN_RESULT_ADDR     0x30040000UL
#endif

/**
 * @brief U-Boot 阶段结果区域最大大小 (bytes)
 *
 * U-Boot阶段DDR空间充足，可设置较大的结果区域以记录详细信息。
 */
#ifndef PLAT_UBOOT_AUTORUN_RESULT_SIZE
#define PLAT_UBOOT_AUTORUN_RESULT_SIZE     32768
#endif

/**
 * @brief U-Boot 定时器频率 (MHz)
 *
 * 用于时间计算。默认值24MHz是常见的ARM SoC定时器频率。
 * 实际值应从 CONFIG_SYS_HZ 或 timer驱动获取。
 */
#ifndef UBOOT_TIMER_FREQ_MHZ
#define UBOOT_TIMER_FREQ_MHZ               24UL
#endif

/* ================================================================== */
/*                   核心API函数声明                                  */
/* ================================================================== */

/**
 * @brief 初始化并运行U-Boot阶段的自动任务
 *
 * 这是U-Boot适配层的入口函数，完成以下工作:
 * 1. 准备平台操作函数表 (映射到U-Boot API)
 * 2. 调用 autorun_init() 初始化框架
 * 3. 调用 autorun_run_stage("uboot") 执行所有U-Boot任务
 * 4. 输出简要结果摘要到控制台
 *
 * @return int
 *         0 = 所有任务通过
 *         非0 = 有失败任务或初始化错误
 */
int uboot_run_autorun(void);

/**
 * @brief 使用自定义配置地址运行自动任务
 *
 * @param[in] config_addr 配置数据的物理地址
 * @param[in] result_addr 结果区域的物理地址
 * @param[in] result_size 结果区域大小 (bytes)
 *
 * @return int 0=成功, 非0=错误
 */
int uboot_run_autorun_with_addr(uint32_t config_addr,
                                uint32_t result_addr,
                                uint32_t result_size);

/**
 * @brief 获取U-Boot适配器的平台操作表
 *
 * @return const autorun_platform_ops_t* 平台操作表指针 (静态存储)
 */
const autorun_platform_ops_t* uboot_get_platform_ops(void);

/* ================================================================== */
/*                    状态查询                                       */
/* ================================================================== */

/**
 * @brief 检查U-Boot自动运行是否已完成
 *
 * @return bool true=已完成, false=未执行或正在执行
 */
bool uboot_is_autorun_completed(void);

/**
 * @brief 获取最后一次执行的返回码
 *
 * @return int 最后一次 uboot_run_autorun() 的返回值
 */
int uboot_get_last_result(void);

#ifdef __cplusplus
}
#endif

#endif /* UBOOT_ADAPTER_H */

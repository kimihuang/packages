/**
 * @file emu_trigger.h
 * @brief 仿真环境(Emulator)波形抓取 trigger 机制
 *
 * 设计原理:
 *   在 SoC 中预留一个 reserved 寄存器，仿真工具监控该寄存器的 MMIO 写操作。
 *   软件在需要抓取波形的关键点向该寄存器的某个 bit 写入 1，
 *   仿真工具检测到写操作后立即开始/标记波形抓取。
 *
 * 使用方式:
 *   1. 在编译选项中指定预留寄存器地址:
 *        gcc -DEMU_TRIGGER_REG_ADDR=0x60010000 ...
 *      或在包含此头文件之前 #define EMU_TRIGGER_REG_ADDR
 *
 *   2. 在代码中埋入触发点:
 *        emu_trigger_set(0, 1);   // bit0 置 1, 仿真开始抓取波形
 *        ...
 *        emu_trigger_set(0, 0);   // bit0 清 0, 仿真停止抓取(可选)
 *
 *   3. 测试完成后清除所有 trigger:
 *        emu_trigger_clear_all();
 *
 * 仿真工具侧配置:
 *   仿真工具需配置 monitor/watchpoint, 监控地址 EMU_TRIGGER_REG_ADDR
 *   的写操作。当检测到写操作时，按 bit 位含义决定抓取行为。
 */

#ifndef EMU_TRIGGER_H
#define EMU_TRIGGER_H

#include <stdint.h>

/* ========================================================================
 * 触发寄存器地址配置
 *
 * 用户必须在编译时定义 EMU_TRIGGER_REG_ADDR，指向预留寄存器的物理地址。
 *
 * 典型选址:
 *   - SoC 中空闲的 scratch/general-purpose 寄存器
 *   - 未被外设使用的地址空间
 *   - 仿真环境专用的 debug 总线地址范围
 *
 * 示例: -DEMU_TRIGGER_REG_ADDR=0x60010000
 * ======================================================================== */
#ifndef EMU_TRIGGER_REG_ADDR
#error "EMU_TRIGGER_REG_ADDR is not defined. " \
       "Please define the reserved trigger register address, " \
       "e.g. gcc -DEMU_TRIGGER_REG_ADDR=0x60010000"
#endif

/* 寄存器宽度: 32-bit, 可根据需要修改为 64-bit */
#define EMU_TRIGGER_REG_WIDTH  32

/* 触发寄存器指针: volatile 保证每次访问都产生实际的 MMIO 读/写操作 */
#define EMU_TRIGGER_REG  ((volatile uint32_t *)(uintptr_t)(EMU_TRIGGER_REG_ADDR))

/**
 * @brief 设置触发寄存器的指定 bit
 *
 * 功能: 对寄存器做读-改-写操作，将 bit 位设置为 value。
 *       不影响其他 bit 位，因此不同 trigger 点可以使用不同的 bit 编码。
 *
 * @param bit   目标位索引，范围 [0, 31]
 * @param value 写入值: 0 = 清零该位, 1 = 置位该位
 *
 * 使用场景:
 *   - emu_trigger_set(3, 1): 设置 bit3 = 1, 仿真工具可能据此开始抓取
 *   - emu_trigger_set(3, 0): 设置 bit3 = 0, 仿真工具可能据此停止抓取
 *   - 不同事件用不同 bit: bit0=DMA开始, bit1=中断进入, bit2=数据就绪
 *
 * 注意事项:
 *   - 函数为 static inline, 无调用开销
 *   - 非中断安全: 若 ISR 中也需要调用, 需确保与主线程不冲突
 *     (不同 bit 之间无冲突, 同一 bit 需外部同步保护)
 *   - 在带 MMU/Cache 的系统中, 需确保该地址映射为 Device/nGnRnE 类型
 *     以保证写操作按预期到达总线
 */
static inline void emu_trigger_set(uint8_t bit, uint8_t value)
{
    uint32_t tmp;

    tmp  = *EMU_TRIGGER_REG;
    if (value) {
        tmp |= (1UL << bit);
    } else {
        tmp &= ~(1UL << bit);
    }
    *EMU_TRIGGER_REG = tmp;
}

/**
 * @brief 清除触发寄存器的全部位域
 *
 * 功能: 将整个寄存器写 0, 清除所有 trigger 标志。
 *       通常在测试初始化阶段或测试结束时调用，恢复寄存器到默认状态。
 *
 * 使用场景:
 *   - 测试用例入口: 清除上次残留的 trigger 状态
 *   - 测试用例出口: 确认所有 trigger 已关闭
 *   - 重新开始抓取前: 清空旧状态
 */
static inline void emu_trigger_clear_all(void)
{
    *EMU_TRIGGER_REG = 0;
}

#endif /* EMU_TRIGGER_H */

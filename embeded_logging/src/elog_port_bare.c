/**
 * @file elog_port_bare.c
 * @brief Port 层 Bare-metal 桩实现
 *
 * 提供最小化的空操作/简单实现，适用于无 OS 的嵌入式环境。
 * 用户可根据具体 MCU 替换其中 RTC/中断控制部分。
 */

#include "elog_port.h"

/* ===== 同步原语 (单线程: 空操作) ===== */

void elog_mutex_init(elog_mutex_t* m)    { (void)m; }
void elog_mutex_destroy(elog_mutex_t* m) { (void)m; }
void elog_mutex_lock(elog_mutex_t* m)    { (void)m; }
void elog_mutex_unlock(elog_mutex_t* m)  { (void)m; }

void elog_cond_init(elog_cond_t* c)    { (void)c; }
void elog_cond_destroy(elog_cond_t* c) { (void)c; }
void elog_cond_signal(elog_cond_t* c)  { (void)c; }
void elog_cond_wait(elog_cond_t* c, elog_mutex_t* m) { (void)c; (void)m; }

/* ===== 时间 (用户需根据 MCU RTC 实现) ===== */

uint32_t elog_port_now(void) {
    /* TODO: 读取 MCU RTC 寄存器 */
    return 0;
}

void elog_port_localtime(uint32_t timestamp,
                         int* hour, int* minute, int* second,
                         int* day, int* month) {
    /*
     * 简单算法: 1970-01-01 00:00:00 起的秒数 → 日历字段
     * 精度足够嵌入式日志时间戳 (年可后续扩展)
     */
    uint32_t days = timestamp / 86400;
    uint32_t secs = timestamp % 86400;

    /* 从 1970-01-01 起算天数 → 月日 */
    static const uint8_t days_in_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    int y = 1970;
    while (1) {
        int days_in_year = 365 + ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0);
        if (days < (uint32_t)days_in_year) break;
        days -= days_in_year;
        y++;
    }

    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
    int m_idx = 0;
    for (; m_idx < 12; m_idx++) {
        int dim = days_in_month[m_idx];
        if (m_idx == 1 && leap) dim = 29;
        if (days < (uint32_t)dim) break;
        days -= dim;
    }

    if (hour)   *hour   = (int)(secs / 3600);
    if (minute) *minute = (int)((secs % 3600) / 60);
    if (second) *second = (int)(secs % 60);
    if (day)    *day    = (int)(days + 1);
    if (month)  *month  = m_idx + 1;
}

/* ===== 进程/线程 ID ===== */

uint16_t elog_port_getpid(void) { return 0; }
uint16_t elog_port_gettid(void) { return 0; }

/* ===== 原子操作 (关中断保护) ===== */

uint32_t elog_port_atomic_inc(volatile uint32_t* val) {
    elog_isr_state_t state = elog_port_isr_save();
    uint32_t old = *val;
    (*val)++;
    elog_port_isr_restore(state);
    return old;
}

/* ===== 中断控制 ===== */

elog_isr_state_t elog_port_isr_save(void) {
    /* TODO: 读取 MCU 中断状态寄存器并禁用全局中断
     * ARM: __asm__ volatile("mrs %0, primask" : "=r"(state)); __disable_irq();
     */
    return 0;
}

void elog_port_isr_restore(elog_isr_state_t state) {
    /* TODO: 恢复中断状态
     * ARM: __asm__ volatile("msr primask, %0" : : "r"(state));
     */
    (void)state;
}

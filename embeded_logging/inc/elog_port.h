/**
 * @file elog_port.h
 * @brief 平台适配层 (Port Layer)
 *
 * 将 pthread、time、pid/tid、atomic 等平台相关 API 抽象为统一接口。
 * 用户通过 elog_config.h 中的 ELOG_PORT_LINUX 宏选择平台实现。
 *
 * 支持的平台:
 *   - Linux (pthread, POSIX time, syscall)
 *   - Bare-metal (关中断临界区, 用户实现 RTC)
 */

#ifndef ELOG_PORT_H
#define ELOG_PORT_H

#include "elog_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 同步原语 ===== */

typedef struct __attribute__((aligned(8))) {
    /* platform opaque, Linux: pthread_mutex_t */
    uint8_t _opaque[64];
} elog_mutex_t;

typedef struct __attribute__((aligned(8))) {
    /* platform opaque, Linux: pthread_cond_t */
    uint8_t _opaque[64];
} elog_cond_t;

void elog_mutex_init(elog_mutex_t* m);
void elog_mutex_destroy(elog_mutex_t* m);
void elog_mutex_lock(elog_mutex_t* m);
void elog_mutex_unlock(elog_mutex_t* m);

void elog_cond_init(elog_cond_t* c);
void elog_cond_destroy(elog_cond_t* c);
void elog_cond_signal(elog_cond_t* c);
void elog_cond_wait(elog_cond_t* c, elog_mutex_t* m);

/**
 * 带超时的条件变量等待
 * @param c           条件变量
 * @param m           互斥锁 (必须已锁定)
 * @param timeout_ms  超时毫秒 (0=立即返回)
 * @return ELOG_OK=被唤醒, ELOG_ERR_BUSY=超时
 */
int elog_cond_timedwait(elog_cond_t* c, elog_mutex_t* m, int timeout_ms);

/* ===== 时间 ===== */

/**
 * 获取当前 Unix 时间戳 (秒)
 * @return 当前时间，或 0 (bare-metal 未实现 RTC)
 */
uint32_t elog_port_now(void);

/**
 * 将 Unix 时间戳分解为时间字段
 * @param timestamp  Unix 时间戳 (秒)
 * @param hour       时 (0-23)
 * @param minute     分 (0-59)
 * @param second     秒 (0-59)
 * @param day        日 (1-31)
 * @param month      月 (1-12)
 */
void elog_port_localtime(uint32_t timestamp,
                         int* hour, int* minute, int* second,
                         int* day, int* month);

/* ===== 进程/线程 ID ===== */

/**
 * 获取进程 ID
 * @return PID，或 0 (bare-metal)
 */
uint16_t elog_port_getpid(void);

/**
 * 获取线程 ID
 * @return TID，或 0 (bare-metal)
 */
uint16_t elog_port_gettid(void);

/* ===== 原子操作 ===== */

/**
 * 原子递增
 * @return 递增前的旧值
 */
uint32_t elog_port_atomic_inc(volatile uint32_t* val);

/* ===== 中断控制 (bare-metal 用) ===== */

typedef unsigned int elog_isr_state_t;

/**
 * 保存并禁用中断 (bare-metal: 读取并关闭全局中断)
 * @return 中断状态 (用于 restore)
 */
elog_isr_state_t elog_port_isr_save(void);

/**
 * 恢复中断状态
 */
void elog_port_isr_restore(elog_isr_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_PORT_H */

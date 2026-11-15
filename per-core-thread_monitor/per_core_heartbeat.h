/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * per_core_heartbeat.h - 每核心绑核线程软心跳监控模块头文件
 *
 * 方案 B：每个目标核心绑定一个独立的内核线程，线程周期性地自增本核心的
 * 心跳计数；监控线程通过检查计数是否持续推进，判断该核心是否仍能被调度器
 * 正常调度，从而发现"软死锁"或"调度饿死"的核心。
 *
 * 本头文件定义：
 *  - 每核心心跳状态结构体（per-cpu 变量存储）
 *  - 全局监控上下文
 *  - 核心状态枚举
 */

#ifndef _PER_CORE_HEARTBEAT_H
#define _PER_CORE_HEARTBEAT_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

struct task_struct;

/*
 * enum per_core_hb_cpu_status - 每核心监控状态
 *
 * @PER_CORE_HB_OK:           心跳持续推进，核心可被正常调度
 * @PER_CORE_HB_STALLED:      软性失联，连续 miss 次数达到 stall_threshold
 * @PER_CORE_HB_UNRESPONSIVE: 硬性失联，连续 miss 次数达到 unresponsive_threshold
 * @PER_CORE_HB_OFFLINE:      核心已下线，不参与监控
 * @PER_CORE_HB_SELF:         监控核心自身，不参与心跳轮询
 */
enum per_core_hb_cpu_status {
	PER_CORE_HB_OK           = 0,
	PER_CORE_HB_STALLED      = 1,
	PER_CORE_HB_UNRESPONSIVE = 2,
	PER_CORE_HB_OFFLINE      = 3,
	PER_CORE_HB_SELF         = 4,
};

/*
 * struct per_core_hb_cpu_state - 每核心心跳监控状态
 *
 * 每个 CPU 一份（通过 DEFINE_PER_CPU 分配）。不同成员由不同上下文访问：
 *
 * @heartbeat_counter: 心跳计数。仅对应核心的绑核心跳线程通过 atomic64_inc
 *                     自增，监控线程通过 atomic64_read 读取。跨核心访问依赖
 *                     原子操作，无需额外锁。
 * @last_seen:         监控线程最近一次观察到的 counter 快照，仅监控线程读写。
 * @miss_count:        连续"心跳未推进"的检查周期数，仅监控线程读写。
 * @state:             当前状态（enum per_core_hb_cpu_status），监控线程写，
 *                     procfs 读，通过 READ_ONCE/WRITE_ONCE 保证可见性。
 */
struct per_core_hb_cpu_state {
	atomic64_t heartbeat_counter;
	u64        last_seen;
	u32        miss_count;
	u32        state;
};

/*
 * struct per_core_hb_ctx - 全局监控上下文
 *
 * @monitor_thread: 监控内核线程 task_struct，绑核到 monitor_cpu
 * @hb_threads:     按 CPU 编号索引的心跳线程 task_struct 指针数组，仅目标核心
 *                  位置非空
 * @target_mask:    目标核心掩码（cpu_online_mask 减去 monitor_cpu）
 * @monitor_cpu:    监控核心编号（模块参数，加载时校验）
 * @nr_targets:     当前参与监控的目标核心数量
 */
struct per_core_hb_ctx {
	struct task_struct  *monitor_thread;
	struct task_struct **hb_threads;
	struct cpumask       target_mask;
	unsigned int         monitor_cpu;
	unsigned int         nr_targets;
};

#endif /* _PER_CORE_HEARTBEAT_H */

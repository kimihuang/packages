/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ipi_monitor.h - 环状双向互监模块头文件
 *
 * 每个 core 运行一个固定在本核的硬中断 hrtimer：
 *   - 自增本核 heartbeat_counter（自证"本核能响应中断"）
 *   - 监控环上的前驱核与后继核（双向），读取它们的 counter 并判定健康状态
 *
 * 每个 core 因此被两个邻居（前驱 + 后继）监控，消除单监控核单点；
 * 心跳信号由本核硬中断产生，不再依赖线程调度，也不再使用 IPI。
 *
 * 覆盖故障：L2（关中断/关抢占硬死锁）、L4（核心硬件失效）。
 */

#ifndef _IPI_MONITOR_H
#define _IPI_MONITOR_H

#include <linux/types.h>
#include <linux/atomic.h>

/* 每个 core 监控的 peer 数：前驱 + 后继 */
#define IPI_MONITOR_NR_PEERS 2

/*
 * enum ipi_monitor_cpu_status - 每核健康状态
 *
 * @IPI_MONITOR_OK:           心跳持续推进，正常
 * @IPI_MONITOR_STALLED:      连续 miss 达到 stall_threshold
 * @IPI_MONITOR_UNRESPONSIVE: 连续 miss 达到 unresponsive_threshold
 * @IPI_MONITOR_OFFLINE:      核心已下线
 */
enum ipi_monitor_cpu_status {
	IPI_MONITOR_OK           = 0,
	IPI_MONITOR_STALLED      = 1,
	IPI_MONITOR_UNRESPONSIVE = 2,
	IPI_MONITOR_OFFLINE      = 3,
};

/*
 * struct ipi_monitor_cpu_state - 每核状态（DEFINE_PER_CPU 分配）
 *
 * @heartbeat_counter: 本核自心跳计数。仅本核 hrtimer 在硬中断里 atomic64_inc；
 *                     其余核（监控者）通过 atomic64_read 读取。跨核无锁。
 * @peer_cpu:          本核监控的 peer 编号（前驱/后继），-1 表示无。仅本核读写。
 * @peer_last_seen:    本核对每个 peer counter 的快照。仅本核读写。
 * @peer_miss:         本核判定的每个 peer 连续 miss 次数。仅本核读写。
 * @peer_state:        本核判定的每个 peer 健康状态。仅本核读写；供上报与 procfs 聚合读取。
 * @reported_state:    已上报过的聚合状态缓存。仅上报 work 读写（进程上下文单写者）。
 *
 * 并发模型：除 heartbeat_counter（atomic64）与 reported_state（上报 work 单写者）
 * 外，其余字段均只有"本核 hrtimer"这一个写者，跨核只读（procfs 聚合用 READ_ONCE），
 * 因此无需锁。
 */
struct ipi_monitor_cpu_state {
	atomic64_t heartbeat_counter;

	int  peer_cpu[IPI_MONITOR_NR_PEERS];
	u64  peer_last_seen[IPI_MONITOR_NR_PEERS];
	u32  peer_miss[IPI_MONITOR_NR_PEERS];
	u32  peer_state[IPI_MONITOR_NR_PEERS];

	u32  reported_state;
};

#endif /* _IPI_MONITOR_H */

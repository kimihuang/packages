/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux_subsys_time_sync.h - 跨核时间同步锚点对广播模块头文件
 *
 * 本模块在 Linux 侧生成时间同步锚点对 (cntvct_snap, wall_snap)，写入
 * 共享内存供 RT-Thread（小核）读取，用于把两核日志时间戳对齐到同一
 * 墙钟时间轴。
 *
 * 本头文件定义共享内存中的锚点数据结构（Linux 写，RT-Thread 读）。
 */

#ifndef _LINUX_SUBSYS_TIME_SYNC_H
#define _LINUX_SUBSYS_TIME_SYNC_H

#include <linux/types.h>

/* 共享内存魔数，RT-Thread 侧据此校验数据有效性 */
#define TIME_SYNC_MAGIC	0x54494D4553594E43ULL	/* "TIMESYNC" */

/*
 * struct time_sync_anchor - 跨核时间同步锚点（共享内存布局）
 *
 * Linux 周期性写入，RT-Thread 读取。通过 @seq 实现 seqlock 语义：
 *
 *   @seq 为奇数  → 正在更新，读者应重试
 *   @seq 为偶数  → 数据稳定，可安全读取
 *
 * 读取协议（RT-Thread 侧）：
 *   do {
 *       s1 = seq;                    // 必须是偶数
 *       cntvct = cntvct_snap;
 *       wall   = wall_snap;
 *       freq   = freq;
 *       s2 = seq;
 *   } while (s1 != s2 || (s1 & 1));
 *
 * 换算公式（RT-Thread 侧）：
 *   wall_now = wall_snap + (cntvct_now - cntvct_snap) * 1e9 / freq
 *
 * @magic:       魔数 TIME_SYNC_MAGIC，初始化时写一次
 * @seq:         seqlock 序列号，奇数=更新中，偶数=稳定
 * @cntvct_snap: 硬件计数器 CNTVCT_EL0 快照（与 wall_snap 同一时刻）
 * @wall_snap:   墙钟快照（纳秒，自 Unix epoch）
 * @freq:        计数器频率（Hz）
 * @reserved:    预留对齐
 */
struct time_sync_anchor {
	u64 magic;
	u64 seq;
	u64 cntvct_snap;
	u64 wall_snap;
	u32 freq;
	u32 reserved;
};

#endif /* _LINUX_SUBSYS_TIME_SYNC_H */

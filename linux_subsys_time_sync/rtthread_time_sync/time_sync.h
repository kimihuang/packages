/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * time_sync.h - 跨核时间同步锚点对读取组件（RT-Thread 侧）
 *
 * 读取 Linux（大核）通过共享内存广播的时间同步锚点对 (cntvct_snap,
 * wall_snap)，结合本核读取的硬件计数器 CNTVCT_EL0，换算成与 Linux
 * 对齐的墙钟时间，用于把两核日志时间戳对齐到同一墙钟时间轴。
 *
 * 本头文件定义共享内存锚点结构（与 Linux 侧 linux_subsys_time_sync.h
 * 的 struct time_sync_anchor 布局完全一致）。
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <rtthread.h>

/* 共享内存魔数，与 Linux 侧一致 */
#define TIME_SYNC_MAGIC  0x54494D4553594E43ULL   /* "TIMESYNC" */

/* 共享内存基地址（虚拟地址，由 BSP 映射 reserved memory 得到）
 * 需通过 rtconfig.h 或 Kconfig 配置，0 表示未配置 */
#ifndef TIME_SYNC_SHARED_MEM_ADDR
#define TIME_SYNC_SHARED_MEM_ADDR  0
#endif

/*
 * struct time_sync_anchor - 跨核时间同步锚点（共享内存布局）
 *
 * Linux 周期性写入，RT-Thread 读取。与 Linux 侧 struct time_sync_anchor
 * 完全一致（共 32 字节）。
 *
 * @magic:       魔数 TIME_SYNC_MAGIC
 * @seq:         seqlock 序列号：奇数=更新中，偶数=稳定
 * @cntvct_snap: 硬件计数器 CNTVCT_EL0 快照
 * @wall_snap:   墙钟快照（纳秒，自 Unix epoch）
 * @freq:        计数器频率（Hz）
 * @reserved:    预留对齐
 */
struct time_sync_anchor
{
    rt_uint64_t magic;
    rt_uint64_t seq;
    rt_uint64_t cntvct_snap;
    rt_uint64_t wall_snap;
    rt_uint32_t freq;
    rt_uint32_t reserved;
};

/**
 * time_sync_init - 初始化并校验共享内存锚点
 *
 * 校验共享内存中的 magic 是否为 TIME_SYNC_MAGIC。
 *
 * Return: RT_EOK 成功；-RT_ERROR 未配置地址或 magic 不匹配
 */
rt_err_t time_sync_init(void);

/**
 * time_sync_get_wallclock_ns - 获取墙钟时间（纳秒，自 epoch）
 *
 * 读取共享内存锚点对 + 本核 CNTVCT，按 seqlock 协议计算：
 *
 *     wall_now = wall_snap + (cntvct_now - cntvct_snap) * 1e9 / freq
 *
 * Return: 墙钟纳秒；0 表示锚点无效（未初始化或 magic 不匹配）
 */
rt_uint64_t time_sync_get_wallclock_ns(void);

/**
 * time_sync_get_wallclock_us - 获取墙钟时间（微秒，自 epoch）
 *
 * = time_sync_get_wallclock_ns() / 1000，便于 ulog 等消费方直接使用。
 *
 * Return: 墙钟微秒；0 表示锚点无效
 */
rt_uint64_t time_sync_get_wallclock_us(void);

/**
 * time_sync_get_wallclock_sec_us - 拆出墙钟的秒 + 微秒
 *
 * @sec:  输出秒（自 epoch）
 * @usec: 输出微秒
 *
 * 供 gettimeofday 等接口接入使用：在 libc 的 gettimeofday() 里 RTC 读取
 * 失败后调用本函数，把 sec/usec 填入 struct timeval。
 *
 * Return: 0 成功；-1 参数为空或锚点无效
 */
int time_sync_get_wallclock_sec_us(rt_uint64_t *sec, rt_uint32_t *usec);

#endif /* TIME_SYNC_H */

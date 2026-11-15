/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * time_sync.c - 跨核时间同步锚点对读取组件实现（RT-Thread 侧）
 *
 * 读取 Linux（大核）广播到共享内存的时间同步锚点对，结合本核读取的
 * ARM 架构定时器系统计数器 CNTVCT_EL0，换算成与 Linux 对齐的墙钟。
 *
 * 换算公式（详见 spec_research/linux/time/rtthread_linux_log_timestamp_alignment.md）：
 *
 *     wall_now = wall_snap + (cntvct_now - cntvct_snap) * 1e9 / freq
 *
 * 关键点：
 *  - 两核读同一个物理计数器 CNTVCT，增量天然一致，不存在 tick 漂移。
 *  - 用 seqlock（seq 奇偶）保证读锚点时不被 Linux 侧写入撕裂。
 *  - 用 dmb ish 内存屏障保证跨核读序。
 */

#include "time_sync.h"

/* 内存屏障：保证跨核读序（inner shareable domain） */
#define TIME_SYNC_MB()  __asm__ volatile ("dmb ish" ::: "memory")

/* 读硬件计数器 CNTVCT_EL0（虚拟计数器，与 Linux arch_timer_read_counter 一致） */
rt_inline rt_uint64_t time_sync_read_cntvct(void)
{
    rt_uint64_t val;

    __asm__ volatile ("isb" ::: "memory");
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(val));

    return val;
}

/* 共享内存锚点指针（volatile：防止编译器重排/缓存读） */
#define TIME_SYNC_ANCHOR()  \
    ((volatile struct time_sync_anchor *)TIME_SYNC_SHARED_MEM_ADDR)

/* 当前锚点是否有效 */
static rt_bool_t time_sync_anchor_valid(void)
{
    volatile struct time_sync_anchor *a = TIME_SYNC_ANCHOR();

    if (TIME_SYNC_SHARED_MEM_ADDR == 0 || a == RT_NULL)
        return RT_FALSE;

    return a->magic == TIME_SYNC_MAGIC;
}

/*
 * time_sync_read_anchor - 按 seqlock 协议读取锚点对
 *
 * 读协议（与 Linux 侧写协议配对）：
 *   do {
 *       s1 = seq;                    // 必须是偶数
 *       cntvct = cntvct_snap;
 *       wall   = wall_snap;
 *       freq   = freq;
 *       s2 = seq;
 *   } while (s1 != s2 || (s1 & 1));
 */
static rt_err_t time_sync_read_anchor(rt_uint64_t *cntvct_snap,
                                      rt_uint64_t *wall_snap,
                                      rt_uint32_t *freq)
{
    volatile struct time_sync_anchor *a = TIME_SYNC_ANCHOR();
    rt_uint64_t s1, s2;

    do
    {
        s1 = a->seq;
        if (s1 & 1)
            continue;                    /* 奇数：Linux 正在更新，重试 */

        TIME_SYNC_MB();                  /* 读序屏障 */
        *cntvct_snap = a->cntvct_snap;
        *wall_snap   = a->wall_snap;
        *freq        = a->freq;
        TIME_SYNC_MB();                  /* 读序屏障 */

        s2 = a->seq;
    } while (s1 != s2);                  /* 读取期间被更新，重试 */

    return RT_EOK;
}

/*
 * time_sync_cycles_to_ns - cycle 增量转纳秒（避免中间溢出）
 *
 * 拆成商 + 余数两部分分别乘 1e9，避免 (cycles * 1e9) 中间溢出 u64。
 */
static rt_uint64_t time_sync_cycles_to_ns(rt_uint64_t cycles, rt_uint32_t freq)
{
    rt_uint64_t sec = cycles / freq;
    rt_uint64_t rem = cycles % freq;

    return sec * 1000000000ULL + rem * 1000000000ULL / freq;
}

rt_err_t time_sync_init(void)
{
    if (!time_sync_anchor_valid())
    {
        rt_kprintf("[time_sync] shared mem anchor invalid (addr=0x%lx)\n",
                   (rt_ubase_t)TIME_SYNC_SHARED_MEM_ADDR);
        return -RT_ERROR;
    }

    rt_kprintf("[time_sync] anchor OK (addr=0x%lx)\n",
               (rt_ubase_t)TIME_SYNC_SHARED_MEM_ADDR);
    return RT_EOK;
}

rt_uint64_t time_sync_get_wallclock_ns(void)
{
    rt_uint64_t cntvct_snap, wall_snap, cntvct_now, delta;
    rt_uint32_t freq;

    if (!time_sync_anchor_valid())
        return 0;

    if (time_sync_read_anchor(&cntvct_snap, &wall_snap, &freq) != RT_EOK)
        return 0;

    if (freq == 0)
        return 0;

    cntvct_now = time_sync_read_cntvct();

    /* counter 单调递增，cntvct_now >= cntvct_snap（正常情况） */
    if (cntvct_now < cntvct_snap)
        delta = 0;                       /* 异常回绕，保守返回 wall_snap */
    else
        delta = cntvct_now - cntvct_snap;

    return wall_snap + time_sync_cycles_to_ns(delta, freq);
}

rt_uint64_t time_sync_get_wallclock_us(void)
{
    return time_sync_get_wallclock_ns() / 1000ULL;
}

int time_sync_get_wallclock_sec_us(rt_uint64_t *sec, rt_uint32_t *usec)
{
    rt_uint64_t ns;

    if (sec == RT_NULL || usec == RT_NULL)
        return -1;

    ns = time_sync_get_wallclock_ns();
    if (ns == 0)
        return -1;

    *sec  = ns / 1000000000ULL;
    *usec = (rt_uint32_t)((ns % 1000000000ULL) / 1000ULL);

    return 0;
}

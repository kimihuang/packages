// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux_subsys_time_sync.c - 跨核时间同步锚点对广播模块实现
 *
 * 在 Linux（大核）侧生成时间同步锚点对 (cntvct_snap, wall_snap)，写入
 * 共享内存供 RT-Thread（小核）读取，用于把两核日志时间戳对齐到同一墙钟
 * 时间轴。
 *
 * 关键设计：
 *  - 三明治夹逼（sandwich）生成一致的锚点对：读 CNTVCT → 读墙钟 → 再读
 *    CNTVCT，取两次 counter 的中点，消除读 counter 与读墙钟之间的 skew。
 *  - 锚点对写入共享内存用 seqlock 语义（seq 奇偶）保证跨核读一致性。
 *  - 周期 kthread 重广播：NTP 步进（settimeofday）会跳变墙钟，周期广播
 *    自动把新的墙钟基准传播给 RT-Thread，无需内核打补丁 hook。
 *
 * 时间来源（详见 spec_research/linux/time/timekeeping_analysis.md §11）：
 *  - 计数器：ARM 架构定时器系统计数器 CNTVCT_EL0（arch_timer_read_counter）
 *  - 墙钟：  CLOCK_REALTIME（ktime_get_real_ns，经 NTP 校准）
 *  - 频率：  CNTFRQ_EL0（arch_timer_get_cntfrq）
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>

#include <asm/arch_timer.h>			/* arch_timer_get_cntfrq() */
#include <clocksource/arm_arch_timer.h>		/* arch_timer_read_counter() */

#include "linux_subsys_time_sync.h"

#define MODULE_NAME	"linux_subsys_time_sync"
#define PROC_FILENAME	"linux_subsys_time_sync"

/* ========== 模块参数 ========== */
/* 共享内存物理地址（reserved memory），0 = 禁用共享内存广播 */
static unsigned long shared_mem_phys;
module_param(shared_mem_phys, ulong, 0444);
MODULE_PARM_DESC(shared_mem_phys,
		 "共享内存物理地址（0=禁用共享内存广播）");

/* 共享内存大小，应 >= sizeof(struct time_sync_anchor) */
static unsigned long shared_mem_size = sizeof(struct time_sync_anchor);
module_param(shared_mem_size, ulong, 0444);
MODULE_PARM_DESC(shared_mem_size, "共享内存大小");

/* 周期广播间隔（毫秒），0 = 仅初始化时广播一次 */
static unsigned int broadcast_interval_ms = 1000;
module_param(broadcast_interval_ms, uint, 0644);
MODULE_PARM_DESC(broadcast_interval_ms,
		 "周期广播间隔（毫秒，0=仅初始化广播一次）");

/* 计数器频率覆盖（Hz），0 = 从 CNTFRQ_EL0 自动获取 */
static unsigned int freq_override;
module_param(freq_override, uint, 0444);
MODULE_PARM_DESC(freq_override,
		 "计数器频率覆盖（0=从 CNTFRQ_EL0 自动获取）");

/* ========== 全局状态 ========== */
static struct time_sync_anchor *g_anchor;	/* 映射后的共享内存 */
static struct task_struct *g_thread;		/* 广播线程 */
static struct proc_dir_entry *proc_file;

/*
 * time_sync_get_freq - 获取计数器频率
 *
 * 优先用 freq_override（若配置），否则读 CNTFRQ_EL0。
 */
static u32 time_sync_get_freq(void)
{
	if (freq_override)
		return freq_override;
	return arch_timer_get_cntfrq();		/* 读 CNTFRQ_EL0 */
}

/*
 * time_sync_generate_anchor - 三明治夹逼生成锚点对
 * @cntvct: 输出计数器快照
 * @wall:   输出墙钟快照（纳秒）
 * @freq:   输出计数器频率
 *
 * 读 counter → 读墙钟 → 再读 counter，取两次 counter 中点作为与墙钟
 * 最接近的 counter 值，消除二者读取时刻不同引入的 skew（几十纳秒级）。
 */
static void time_sync_generate_anchor(u64 *cntvct, u64 *wall, u32 *freq)
{
	u64 c1, c2;

	c1 = arch_timer_read_counter();
	*wall = ktime_get_real_ns();		/* 内部会再读一次 counter */
	c2 = arch_timer_read_counter();

	/* counter 单调递增，c2 >= c1，用 (c2-c1)/2 避免加法溢出 */
	*cntvct = c1 + ((c2 - c1) >> 1);
	*freq = time_sync_get_freq();
}

/*
 * time_sync_broadcast - 把锚点对写入共享内存（seqlock 语义）
 *
 * 写协议：seq 变奇数 → 写字段 → seq 变偶数。RT-Thread 读时用
 * seq 前后一致且为偶数判断数据稳定。
 */
static void time_sync_broadcast(void)
{
	struct time_sync_anchor *a = g_anchor;
	u64 cntvct, wall, seq;
	u32 freq;

	if (!a)
		return;

	time_sync_generate_anchor(&cntvct, &wall, &freq);

	seq = READ_ONCE(a->seq);
	WRITE_ONCE(a->seq, seq + 1);		/* 奇数：开始更新 */
	smp_wmb();
	WRITE_ONCE(a->cntvct_snap, cntvct);
	WRITE_ONCE(a->wall_snap, wall);
	WRITE_ONCE(a->freq, freq);
	smp_wmb();
	WRITE_ONCE(a->seq, seq + 2);		/* 偶数：更新完成 */
}

/*
 * time_sync_thread_fn - 周期广播线程
 *
 * 周期重广播的目的：NTP 步进会跳变墙钟，周期广播自动把新的墙钟基准
 * 传播给 RT-Thread，无需 hook 内核时间设置路径。
 */
static int time_sync_thread_fn(void *data)
{
	while (!kthread_should_stop()) {
		time_sync_broadcast();
		schedule_timeout_interruptible(msecs_to_jiffies(broadcast_interval_ms));
	}

	return 0;
}

/* ========== procfs 接口 ========== */
static int time_sync_show(struct seq_file *m, void *v)
{
	struct timespec64 ts;
	u64 cntvct, wall;
	u32 freq;

	/* 实时生成一次锚点，展示当前换算关系 */
	time_sync_generate_anchor(&cntvct, &wall, &freq);
	ktime_get_real_ts64(&ts);

	seq_puts(m, "live anchor (generated on read):\n");
	seq_printf(m, "  cntvct_snap = %llu\n", (unsigned long long)cntvct);
	seq_printf(m, "  wall_snap   = %llu ns\n", (unsigned long long)wall);
	seq_printf(m, "  freq        = %u Hz\n", freq);
	seq_printf(m, "  realtime    = %lld.%09ld\n",
		   (long long)ts.tv_sec, ts.tv_nsec);
	seq_puts(m, "\n");

	if (g_anchor) {
		seq_printf(m, "shared memory @ 0x%lx:\n", shared_mem_phys);
		seq_printf(m, "  magic       = 0x%llx\n",
			   (unsigned long long)READ_ONCE(g_anchor->magic));
		seq_printf(m, "  seq         = %llu\n",
			   (unsigned long long)READ_ONCE(g_anchor->seq));
		seq_printf(m, "  cntvct_snap = %llu\n",
			   (unsigned long long)READ_ONCE(g_anchor->cntvct_snap));
		seq_printf(m, "  wall_snap   = %llu ns\n",
			   (unsigned long long)READ_ONCE(g_anchor->wall_snap));
		seq_printf(m, "  freq        = %u Hz\n",
			   READ_ONCE(g_anchor->freq));
	} else {
		seq_puts(m, "shared memory: disabled\n");
	}

	return 0;
}

static int time_sync_open(struct inode *inode, struct file *file)
{
	return single_open(file, time_sync_show, NULL);
}

static const struct proc_ops time_sync_proc_fops = {
	.proc_open	= time_sync_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* ========== 模块初始化 ========== */
static int __init time_sync_init(void)
{
	int ret = 0;

	/* 1. 校验并映射共享内存 */
	if (shared_mem_phys) {
		if (shared_mem_size < sizeof(struct time_sync_anchor)) {
			pr_err("%s: shared_mem_size=%lu 小于锚点结构大小 %zu\n",
			       MODULE_NAME, shared_mem_size,
			       sizeof(struct time_sync_anchor));
			return -EINVAL;
		}
		g_anchor = memremap(shared_mem_phys, shared_mem_size, MEMREMAP_WB);
		if (!g_anchor) {
			pr_err("%s: 映射共享内存 0x%lx 失败\n",
			       MODULE_NAME, shared_mem_phys);
			return -EINVAL;
		}
		/* 初始化魔数与序列号（只写一次） */
		WRITE_ONCE(g_anchor->magic, TIME_SYNC_MAGIC);
		WRITE_ONCE(g_anchor->seq, 0);
	}

	/* 2. 立即广播一次锚点 */
	time_sync_broadcast();

	/* 3. 启动周期广播线程 */
	if (broadcast_interval_ms != 0) {
		g_thread = kthread_run(time_sync_thread_fn, NULL, "time_sync");
		if (IS_ERR(g_thread)) {
			ret = PTR_ERR(g_thread);
			g_thread = NULL;
			pr_err("%s: 创建广播线程失败 (%d)\n", MODULE_NAME, ret);
			goto err_mem;
		}
	}

	/* 4. 创建 procfs 接口 */
	proc_file = proc_create(PROC_FILENAME, 0444, NULL, &time_sync_proc_fops);
	if (!proc_file) {
		pr_err("%s: 创建 /proc/%s 失败\n", MODULE_NAME, PROC_FILENAME);
		ret = -ENOMEM;
		goto err_thread;
	}

	pr_info("%s: 加载成功，freq=%uHz，共享内存=0x%lx，广播间隔=%ums\n",
		MODULE_NAME, time_sync_get_freq(), shared_mem_phys,
		broadcast_interval_ms);
	return 0;

err_thread:
	if (g_thread) {
		kthread_stop(g_thread);
		g_thread = NULL;
	}
err_mem:
	if (g_anchor) {
		memunmap(g_anchor);
		g_anchor = NULL;
	}
	return ret;
}

/* ========== 模块卸载 ========== */
static void __exit time_sync_exit(void)
{
	remove_proc_entry(PROC_FILENAME, NULL);

	if (g_thread) {
		kthread_stop(g_thread);
		g_thread = NULL;
	}

	if (g_anchor) {
		memunmap(g_anchor);
		g_anchor = NULL;
	}

	pr_info("%s: 已卸载\n", MODULE_NAME);
}

module_init(time_sync_init);
module_exit(time_sync_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Quantum Board");
MODULE_DESCRIPTION("Cross-core time sync anchor broadcast (Linux side)");
MODULE_VERSION("1.0");

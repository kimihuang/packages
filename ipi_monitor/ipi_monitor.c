// SPDX-License-Identifier: GPL-2.0-only
/*
 * ipi_monitor.c - 环状双向互监模块实现（每核 hrtimer 自心跳 + 交叉监控）
 *
 * 方案演进：不再由单一监控核向各目标核发送 IPI，而是每个 core 各运行一个
 * 固定在本核的硬中断 hrtimer：
 *   - hrtimer 回调自增本核 heartbeat_counter（自证"本核能响应中断"）
 *   - 回调读取环上前驱核与后继核的 counter，判定它们是否失联
 *
 * 每个 core 因此被两个邻居（前驱 + 后继）监控，消除单监控核单点；同时
 * 心跳信号由本核硬中断产生，不再依赖线程调度，也不依赖 IPI。对 L2/L4
 * 故障的检测能力与 IPI 方案等效。
 *
 * 覆盖故障：L2（关中断/关抢占硬死锁）、L4（核心硬件失效）。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/cpuhotplug.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/panic.h>
#include <linux/percpu.h>
#include <linux/workqueue.h>

#include "ipi_monitor.h"

#define MODULE_NAME	"ipi_monitor"
#define PROC_FILENAME	"ipi_monitor"

/* ========== 模块参数 ========== */
/* 心跳检查周期（毫秒） */
static unsigned int check_interval_ms = 1000;
module_param(check_interval_ms, uint, 0644);
MODULE_PARM_DESC(check_interval_ms, "心跳检查周期（毫秒）");

/* 判定 STALLED 所需连续 miss 次数 */
static unsigned int stall_threshold = 3;
module_param(stall_threshold, uint, 0644);
MODULE_PARM_DESC(stall_threshold, "判定 STALLED 所需连续 miss 次数");

/* 判定 UNRESPONSIVE 所需连续 miss 次数 */
static unsigned int unresponsive_threshold = 10;
module_param(unresponsive_threshold, uint, 0644);
MODULE_PARM_DESC(unresponsive_threshold, "判定 UNRESPONSIVE 所需连续 miss 次数");

/* UNRESPONSIVE 时是否触发 panic */
static bool panic_on_unresponsive;
module_param(panic_on_unresponsive, bool, 0644);
MODULE_PARM_DESC(panic_on_unresponsive, "UNRESPONSIVE 时是否触发 panic");

/* 加载后是否立即启动监控 */
static bool monitor_enabled = true;
module_param(monitor_enabled, bool, 0644);
MODULE_PARM_DESC(monitor_enabled, "加载后是否立即启动监控");

/* ========== 全局状态 ========== */
static DEFINE_PER_CPU(struct ipi_monitor_cpu_state, ipi_monitor_cpu_states);
static DEFINE_PER_CPU(struct hrtimer, ipi_monitor_timer);
static struct work_struct ipi_monitor_report_work;
static struct proc_dir_entry *proc_file;
static enum cpuhp_state hotplug_state;
static ktime_t ipi_monitor_period;

/* ========== 环上邻居计算 ========== */
/*
 * ipi_monitor_prev_online - 返回比 cpu 小的最大 online 核，无则回绕到最大 online 核
 * ipi_monitor_next_online - 返回比 cpu 大的最小 online 核，无则回绕到最小 online 核
 *
 * 两者仅在本核 online 时返回 -1（表示无 peer 可监控）。
 * 每周期动态计算，天然适配 hotplug（无需维护邻接表）。
 */
static int ipi_monitor_prev_online(int cpu)
{
	int p;

	for (p = cpu - 1; p >= 0; p--)
		if (cpu_online(p))
			return p;
	for (p = nr_cpu_ids - 1; p > cpu; p--)
		if (cpu_online(p))
			return p;
	return -1;
}

static int ipi_monitor_next_online(int cpu)
{
	int n;

	for (n = cpu + 1; n < nr_cpu_ids; n++)
		if (cpu_online(n))
			return n;
	for (n = 0; n < cpu; n++)
		if (cpu_online(n))
			return n;
	return -1;
}

/* ========== 状态名映射 ========== */
static const char *ipi_monitor_state_name(u32 state)
{
	switch (state) {
	case IPI_MONITOR_OK:           return "OK";
	case IPI_MONITOR_STALLED:      return "STALLED";
	case IPI_MONITOR_UNRESPONSIVE: return "UNRESPONSIVE";
	case IPI_MONITOR_OFFLINE:      return "OFFLINE";
	default:                       return "UNKNOWN";
	}
}

/* ========== hrtimer 回调（硬中断上下文，固定本核） ========== */
static enum hrtimer_restart ipi_monitor_tick(struct hrtimer *timer)
{
	struct ipi_monitor_cpu_state *st =
		this_cpu_ptr(&ipi_monitor_cpu_states);
	int me = smp_processor_id();
	int peers[IPI_MONITOR_NR_PEERS];
	int i;

	/* 自心跳：自增本核计数，证明本核能响应中断 */
	atomic64_inc(&st->heartbeat_counter);

	/* 动态计算监控对象：前驱 + 后继（双向环） */
	peers[0] = ipi_monitor_prev_online(me);
	peers[1] = ipi_monitor_next_online(me);
	if (peers[0] == peers[1])	/* 仅 2 核在线时前驱==后继，去重 */
		peers[1] = -1;

	for (i = 0; i < IPI_MONITOR_NR_PEERS; i++) {
		struct ipi_monitor_cpu_state *ps;
		u32 old_state, new_state;
		u64 cur;
		int peer = peers[i];

		if (peer < 0)
			continue;

		/*
		 * hotplug 导致的 peer 身份变化：重置该 slot 的快照并跳过
		 * 本轮比对，避免把"新 peer 的首次快照"误判为 miss。
		 */
		if (st->peer_cpu[i] != peer) {
			ps = per_cpu_ptr(&ipi_monitor_cpu_states, peer);
			st->peer_cpu[i] = peer;
			st->peer_last_seen[i] =
				atomic64_read(&ps->heartbeat_counter);
			st->peer_miss[i] = 0;
			st->peer_state[i] = IPI_MONITOR_OK;
			continue;
		}

		ps = per_cpu_ptr(&ipi_monitor_cpu_states, peer);
		cur = atomic64_read(&ps->heartbeat_counter);
		old_state = st->peer_state[i];

		if (cur != st->peer_last_seen[i]) {
			/* 心跳推进，恢复正常 */
			new_state = IPI_MONITOR_OK;
			st->peer_miss[i] = 0;
		} else {
			/* 心跳未推进，累计连续 miss 次数 */
			u32 miss = st->peer_miss[i] + 1;

			st->peer_miss[i] = miss;
			if (miss >= unresponsive_threshold)
				new_state = IPI_MONITOR_UNRESPONSIVE;
			else if (miss >= stall_threshold)
				new_state = IPI_MONITOR_STALLED;
			else
				new_state = old_state;
		}
		st->peer_last_seen[i] = cur;
		st->peer_state[i] = new_state;

		/* 状态迁移：UNRESPONSIVE 立即 panic，日志延迟到 workqueue */
		if (new_state != old_state) {
			if (new_state == IPI_MONITOR_UNRESPONSIVE &&
			    panic_on_unresponsive)
				panic("%s: cpu%u UNRESPONSIVE",
				      MODULE_NAME, peer);
			schedule_work(&ipi_monitor_report_work);
		}
	}

	hrtimer_forward_now(timer, ipi_monitor_period);
	return HRTIMER_RESTART;
}

/* ========== 状态聚合 ========== */
/*
 * ipi_monitor_aggregate - 汇总某核被所有监控者判定的健康状态
 *
 * 每个 online 核被前驱与后继两个监控者观察，取最差状态与最大 miss。
 */
static void ipi_monitor_aggregate(int cpu, u32 *out_state, u32 *out_miss)
{
	u32 worst = IPI_MONITOR_OK;
	u32 max_miss = 0;
	int m, i;

	for_each_online_cpu(m) {
		struct ipi_monitor_cpu_state *ms =
			per_cpu_ptr(&ipi_monitor_cpu_states, m);

		for (i = 0; i < IPI_MONITOR_NR_PEERS; i++) {
			u32 s, miss;

			if (READ_ONCE(ms->peer_cpu[i]) != cpu)
				continue;
			s = READ_ONCE(ms->peer_state[i]);
			miss = READ_ONCE(ms->peer_miss[i]);
			if (s > worst)
				worst = s;
			if (miss > max_miss)
				max_miss = miss;
		}
	}

	*out_state = worst;
	*out_miss = max_miss;
}

/* ========== 上报 work（进程上下文） ========== */
/*
 * ipi_monitor_report_workfn - 汇总各核状态并打印迁移日志
 *
 * 由 hrtimer 回调在状态迁移时通过 schedule_work() 触发，运行于进程上下文，
 * 避免在硬中断里 printk。按 per-cpu reported_state 缓存去重，仅打印迁移。
 */
static void ipi_monitor_report_workfn(struct work_struct *work)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct ipi_monitor_cpu_state *st =
			per_cpu_ptr(&ipi_monitor_cpu_states, cpu);
		u32 state, miss, old;

		ipi_monitor_aggregate(cpu, &state, &miss);

		old = READ_ONCE(st->reported_state);
		if (state == old)
			continue;
		WRITE_ONCE(st->reported_state, state);

		if (state == IPI_MONITOR_OK)
			pr_warn("%s: cpu%u 恢复 (%s -> OK)\n",
				MODULE_NAME, cpu, ipi_monitor_state_name(old));
		else
			pr_warn("%s: cpu%u 状态迁移 %s -> %s (miss=%u)\n",
				MODULE_NAME, cpu,
				ipi_monitor_state_name(old),
				ipi_monitor_state_name(state), miss);
	}
}

/* ========== procfs 接口 ========== */
static int ipi_monitor_show(struct seq_file *m, void *v)
{
	int cpu;

	seq_printf(m, "%-5s %-14s %-16s %s\n", "CPU", "STATE", "COUNTER", "MISS");

	for_each_possible_cpu(cpu) {
		struct ipi_monitor_cpu_state *st =
			per_cpu_ptr(&ipi_monitor_cpu_states, cpu);
		u64 counter = atomic64_read(&st->heartbeat_counter);
		u32 state, miss;

		if (!cpu_online(cpu)) {
			state = IPI_MONITOR_OFFLINE;
			miss = 0;
		} else {
			ipi_monitor_aggregate(cpu, &state, &miss);
		}

		seq_printf(m, "%-5d %-14s %-16llu %u\n", cpu,
			   ipi_monitor_state_name(state),
			   (unsigned long long)counter, miss);
	}

	seq_printf(m, "\ninterval=%ums stall=%u unresponsive=%u panic=%d online=%u\n",
		   check_interval_ms, stall_threshold, unresponsive_threshold,
		   panic_on_unresponsive ? 1 : 0, num_online_cpus());

	return 0;
}

static int ipi_monitor_open(struct inode *inode, struct file *file)
{
	return single_open(file, ipi_monitor_show, NULL);
}

static const struct proc_ops ipi_monitor_proc_fops = {
	.proc_open	= ipi_monitor_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* ========== CPU hotplug 回调 ========== */
static int ipi_monitor_cpu_online(unsigned int cpu)
{
	struct ipi_monitor_cpu_state *st =
		per_cpu_ptr(&ipi_monitor_cpu_states, cpu);
	struct hrtimer *timer = per_cpu_ptr(&ipi_monitor_timer, cpu);
	int i;

	/* 重置本核状态（首次上线或下线后再次上线） */
	atomic64_set(&st->heartbeat_counter, 0);
	for (i = 0; i < IPI_MONITOR_NR_PEERS; i++) {
		st->peer_cpu[i] = -1;
		st->peer_last_seen[i] = 0;
		st->peer_miss[i] = 0;
		st->peer_state[i] = IPI_MONITOR_OK;
	}
	WRITE_ONCE(st->reported_state, IPI_MONITOR_OK);

	/*
	 * PINNED hrtimer 必须在目标核上初始化（base 指向本核 hrtimer_bases）
	 * 并启动，才能固定到本核。本回调由 cpuhp 框架保证运行于 cpu 上
	 * （hotplug 线程绑核，见 kernel/cpu.c cpuhp_invoke_ap_callback）。
	 */
	hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED_HARD);
	timer->function = ipi_monitor_tick;

	if (!monitor_enabled) {
		pr_info("%s: cpu%u 上线（监控未启用）\n", MODULE_NAME, cpu);
		return 0;
	}

	hrtimer_start(timer, ipi_monitor_period, HRTIMER_MODE_REL_PINNED_HARD);
	pr_info("%s: cpu%u 上线，监控定时器启动\n", MODULE_NAME, cpu);
	return 0;
}

static int ipi_monitor_cpu_offline(unsigned int cpu)
{
	struct hrtimer *timer = per_cpu_ptr(&ipi_monitor_timer, cpu);

	/*
	 * 取消本核 PINNED 定时器（PINNED 定时器不随 hotplug 迁移，必须显式
	 * 取消）。定时器已在上线回调中初始化，取消安全。
	 */
	hrtimer_cancel(timer);

	pr_info("%s: cpu%u 下线，监控定时器停止\n", MODULE_NAME, cpu);
	return 0;
}

/* ========== 模块初始化 ========== */
static int __init ipi_monitor_init(void)
{
	int cpu, ret;

	/* 参数合法性校验 */
	if (check_interval_ms < 10) {
		pr_warn("%s: check_interval_ms=%u 过小，钳制为 10ms\n",
			MODULE_NAME, check_interval_ms);
		check_interval_ms = 10;
	}
	if (stall_threshold == 0)
		stall_threshold = 1;
	if (unresponsive_threshold < stall_threshold)
		unresponsive_threshold = stall_threshold;

	ipi_monitor_period = ms_to_ktime(check_interval_ms);

	/* 初始化每核状态 */
	for_each_possible_cpu(cpu) {
		struct ipi_monitor_cpu_state *st =
			per_cpu_ptr(&ipi_monitor_cpu_states, cpu);

		atomic64_set(&st->heartbeat_counter, 0);
		st->peer_cpu[0] = st->peer_cpu[1] = -1;
		st->peer_last_seen[0] = st->peer_last_seen[1] = 0;
		st->peer_miss[0] = st->peer_miss[1] = 0;
		st->peer_state[0] = st->peer_state[1] = IPI_MONITOR_OK;
		st->reported_state = IPI_MONITOR_OK;
	}

	INIT_WORK(&ipi_monitor_report_work, ipi_monitor_report_workfn);

	/* 注册 CPU hotplug 回调（invoke=true：对已 online 核执行 online 回调） */
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "ipi_monitor",
				ipi_monitor_cpu_online, ipi_monitor_cpu_offline);
	if (ret < 0) {
		pr_err("%s: 注册 CPU hotplug 回调失败 (%d)\n", MODULE_NAME, ret);
		return ret;
	}
	hotplug_state = ret;

	/* 创建 procfs 接口 */
	proc_file = proc_create(PROC_FILENAME, 0444, NULL, &ipi_monitor_proc_fops);
	if (!proc_file) {
		pr_err("%s: 创建 /proc/%s 失败\n", MODULE_NAME, PROC_FILENAME);
		cpuhp_remove_state(hotplug_state);
		return -ENOMEM;
	}

	pr_info("%s: 加载成功（环状双向互监，周期=%ums）\n",
		MODULE_NAME, check_interval_ms);
	return 0;
}

/* ========== 模块卸载 ========== */
static void __exit ipi_monitor_exit(void)
{
	remove_proc_entry(PROC_FILENAME, NULL);

	/* 触发各 online 核的 offline 回调，取消各自 hrtimer */
	cpuhp_remove_state(hotplug_state);

	flush_work(&ipi_monitor_report_work);

	pr_info("%s: 已卸载\n", MODULE_NAME);
}

module_init(ipi_monitor_init);
module_exit(ipi_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Quantum Board");
MODULE_DESCRIPTION("Ring bidirectional per-core health monitor (hrtimer self-heartbeat)");
MODULE_VERSION("2.0");

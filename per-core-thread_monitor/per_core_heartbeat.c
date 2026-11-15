// SPDX-License-Identifier: GPL-2.0-only
/*
 * per_core_heartbeat.c - 每核心绑核线程软心跳监控模块实现
 *
 * 方案 B：每个目标核心绑定一个独立的内核线程，线程周期性地自增本核心的
 * 心跳计数；监控线程通过检查计数是否持续推进，判断该核心是否仍能被调度器
 * 正常调度，从而发现"软死锁"或"调度饿死"的核心。
 *
 * 关键设计：
 *  - 心跳线程通过 kthread_run_on_cpu() 创建并绑核，SCHED_FIFO 最低实时优先级，
 *    避免被普通负载（CFS 任务）饿死导致误报，同时不干扰真正的 RT 任务。
 *  - 监控线程通过 kthread_bind() 绑定到可配置的 monitor_cpu。
 *  - 心跳计数使用 atomic64_t，心跳线程与监控线程间无锁访问。
 *  - CPU hotplug 通过 cpuhp_setup_state() 管理心跳线程生命周期（online 创建、
 *    offline 停止）。
 *
 * 覆盖故障：L1（软死锁），并可与方案 A（ipi_monitor）交叉定位 L2/L4。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/cpumask.h>
#include <linux/cpuhotplug.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/panic.h>
#include <linux/percpu.h>
#include <linux/slab.h>

#include "per_core_heartbeat.h"

#define MODULE_NAME	"per_core_heartbeat"
#define PROC_FILENAME	"per_core_heartbeat"

/* ========== 模块参数 ========== */
/* 监控线程运行的核心编号：加载时校验，非法则回退到第一个 online 核心 */
static unsigned int monitor_cpu;
module_param(monitor_cpu, uint, 0444);
MODULE_PARM_DESC(monitor_cpu,
		 "监控线程运行的核心编号（加载时校验，非法回退第一个 online 核心）");

/* 心跳线程自增周期（毫秒），应小于 check_interval_ms */
static unsigned int heartbeat_interval_ms = 500;
module_param(heartbeat_interval_ms, uint, 0644);
MODULE_PARM_DESC(heartbeat_interval_ms, "心跳线程自增周期（毫秒）");

/* 监控检查周期（毫秒） */
static unsigned int check_interval_ms = 1000;
module_param(check_interval_ms, uint, 0644);
MODULE_PARM_DESC(check_interval_ms, "监控检查周期（毫秒）");

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

/* 模块加载后是否立即启动监控 */
static bool monitor_enabled = true;
module_param(monitor_enabled, bool, 0644);
MODULE_PARM_DESC(monitor_enabled, "模块加载后是否立即启动监控");

/* ========== 全局状态 ========== */
static DEFINE_PER_CPU(struct per_core_hb_cpu_state, per_core_hb_cpu_states);
static struct per_core_hb_ctx per_core_hb_ctx;
static struct proc_dir_entry *proc_file;
static enum cpuhp_state hotplug_state;

/* ========== 状态名映射 ========== */
static const char *per_core_hb_state_name(enum per_core_hb_cpu_status state)
{
	switch (state) {
	case PER_CORE_HB_OK:
		return "OK";
	case PER_CORE_HB_STALLED:
		return "STALLED";
	case PER_CORE_HB_UNRESPONSIVE:
		return "UNRESPONSIVE";
	case PER_CORE_HB_OFFLINE:
		return "OFFLINE";
	case PER_CORE_HB_SELF:
		return "MONITOR";
	default:
		return "UNKNOWN";
	}
}

/*
 * per_core_hb_thread_fn - 心跳线程主循环（目标核心，进程上下文）
 * @data: 未使用
 *
 * 周期性自增本核心心跳计数，证明"本核心仍可被调度"。运行于进程上下文，
 * 可睡眠，无中断上下文约束。
 */
static int per_core_hb_thread_fn(void *data)
{
	struct per_core_hb_cpu_state *st;

	st = this_cpu_ptr(&per_core_hb_cpu_states);

	while (!kthread_should_stop()) {
		/* 自增本核心心跳计数 */
		atomic64_inc(&st->heartbeat_counter);

		/* 睡眠一个心跳周期 */
		schedule_timeout_interruptible(msecs_to_jiffies(heartbeat_interval_ms));
	}

	return 0;
}

/*
 * per_core_hb_monitor_fn - 监控线程主循环
 * @data: 指向全局上下文 struct per_core_hb_ctx
 *
 * 每个检查周期遍历目标核心，比对心跳计数是否相较快照推进，更新状态。
 */
static int per_core_hb_monitor_fn(void *data)
{
	struct per_core_hb_ctx *ctx = data;
	unsigned int cpu;

	while (!kthread_should_stop()) {
		for_each_cpu(cpu, &ctx->target_mask) {
			struct per_core_hb_cpu_state *st;
			enum per_core_hb_cpu_status old_state, new_state;
			u64 cur;

			st = per_cpu_ptr(&per_core_hb_cpu_states, cpu);

			if (!cpu_online(cpu)) {
				WRITE_ONCE(st->state, PER_CORE_HB_OFFLINE);
				WRITE_ONCE(st->miss_count, 0);
				continue;
			}

			cur = atomic64_read(&st->heartbeat_counter);
			old_state = READ_ONCE(st->state);

			if (cur != READ_ONCE(st->last_seen)) {
				/* 心跳推进，恢复正常 */
				new_state = PER_CORE_HB_OK;
				WRITE_ONCE(st->miss_count, 0);
			} else {
				/* 心跳未推进，累计连续 miss 次数 */
				u32 miss = READ_ONCE(st->miss_count) + 1;

				WRITE_ONCE(st->miss_count, miss);
				if (miss >= unresponsive_threshold)
					new_state = PER_CORE_HB_UNRESPONSIVE;
				else if (miss >= stall_threshold)
					new_state = PER_CORE_HB_STALLED;
				else
					new_state = old_state;
			}

			/* 状态迁移时上报 */
			if (new_state != old_state) {
				if (new_state == PER_CORE_HB_OK)
					pr_warn("%s: cpu%u 恢复 (%s -> OK)\n",
						MODULE_NAME, cpu,
						per_core_hb_state_name(old_state));
				else
					pr_warn("%s: cpu%u 状态迁移 %s -> %s (miss=%u)\n",
						MODULE_NAME, cpu,
						per_core_hb_state_name(old_state),
						per_core_hb_state_name(new_state),
						READ_ONCE(st->miss_count));

				if (new_state == PER_CORE_HB_UNRESPONSIVE &&
				    panic_on_unresponsive)
					panic("%s: cpu%u UNRESPONSIVE",
					      MODULE_NAME, cpu);
			}

			WRITE_ONCE(st->state, new_state);
			WRITE_ONCE(st->last_seen, cur);
		}

		schedule_timeout_interruptible(msecs_to_jiffies(check_interval_ms));
	}

	return 0;
}

/* ========== procfs 接口 ========== */
static int per_core_hb_show(struct seq_file *m, void *v)
{
	struct per_core_hb_ctx *ctx = &per_core_hb_ctx;
	unsigned int cpu;

	seq_printf(m, "%-5s %-14s %-16s %s\n", "CPU", "STATE", "COUNTER", "MISS");

	for_each_possible_cpu(cpu) {
		struct per_core_hb_cpu_state *st;
		const char *state;
		u64 counter;

		st = per_cpu_ptr(&per_core_hb_cpu_states, cpu);

		if (cpu == ctx->monitor_cpu) {
			seq_printf(m, "%-5u %-14s %-16s %s\n", cpu,
				   "MONITOR", "-", "-");
			continue;
		}

		counter = atomic64_read(&st->heartbeat_counter);
		if (!cpu_online(cpu))
			state = "OFFLINE";
		else
			state = per_core_hb_state_name(READ_ONCE(st->state));

		seq_printf(m, "%-5u %-14s %-16llu %u\n", cpu, state,
			   (unsigned long long)counter, READ_ONCE(st->miss_count));
	}

	seq_printf(m, "\nmonitor_cpu=%u  hb_interval=%ums  check_interval=%ums  stall=%u  unresponsive=%u  targets=%u\n",
		   ctx->monitor_cpu, heartbeat_interval_ms, check_interval_ms,
		   stall_threshold, unresponsive_threshold, ctx->nr_targets);

	return 0;
}

static int per_core_hb_open(struct inode *inode, struct file *file)
{
	return single_open(file, per_core_hb_show, NULL);
}

static const struct proc_ops per_core_hb_proc_fops = {
	.proc_open	= per_core_hb_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* ========== CPU hotplug 回调 ========== */
static int per_core_hb_cpu_online(unsigned int cpu)
{
	struct per_core_hb_ctx *ctx = &per_core_hb_ctx;
	struct per_core_hb_cpu_state *st;
	struct task_struct *t;

	if (cpu == ctx->monitor_cpu)
		return 0; /* 监控核心自身不创建心跳线程 */

	st = per_cpu_ptr(&per_core_hb_cpu_states, cpu);

	/* 创建并绑定心跳线程到该核心 */
	t = kthread_run_on_cpu(per_core_hb_thread_fn, NULL, cpu,
			       "hb_per_core.%u");
	if (IS_ERR(t)) {
		pr_err("%s: cpu%u 创建心跳线程失败 (%ld)\n",
		       MODULE_NAME, cpu, PTR_ERR(t));
		return PTR_ERR(t);
	}
	/* 设为最低实时优先级：高于 CFS 任务，避免高负载误报，又不干扰 RT 任务 */
	sched_set_fifo_low(t);

	ctx->hb_threads[cpu] = t;
	cpumask_set_cpu(cpu, &ctx->target_mask);
	WRITE_ONCE(st->last_seen, 0);
	WRITE_ONCE(st->miss_count, 0);
	WRITE_ONCE(st->state, PER_CORE_HB_OK);
	ctx->nr_targets++;

	pr_info("%s: cpu%u 上线，心跳线程已创建\n", MODULE_NAME, cpu);
	return 0;
}

static int per_core_hb_cpu_offline(unsigned int cpu)
{
	struct per_core_hb_ctx *ctx = &per_core_hb_ctx;
	struct per_core_hb_cpu_state *st;

	if (cpu == ctx->monitor_cpu) {
		pr_warn("%s: 监控核心 cpu%u 即将下线，监控可能失效\n",
			MODULE_NAME, cpu);
		return 0;
	}

	st = per_cpu_ptr(&per_core_hb_cpu_states, cpu);
	cpumask_clear_cpu(cpu, &ctx->target_mask);

	if (ctx->hb_threads[cpu]) {
		kthread_stop(ctx->hb_threads[cpu]);
		ctx->hb_threads[cpu] = NULL;
	}

	WRITE_ONCE(st->state, PER_CORE_HB_OFFLINE);
	WRITE_ONCE(st->miss_count, 0);
	ctx->nr_targets--;

	pr_info("%s: cpu%u 下线，心跳线程已停止\n", MODULE_NAME, cpu);
	return 0;
}

/* ========== 模块初始化 ========== */
static int __init per_core_hb_init(void)
{
	struct per_core_hb_ctx *ctx = &per_core_hb_ctx;
	unsigned int cpu;
	int ret;

	/* 1. 参数合法性校验 */
	if (check_interval_ms < 10) {
		pr_warn("%s: check_interval_ms=%u 过小，钳制为 10ms\n",
			MODULE_NAME, check_interval_ms);
		check_interval_ms = 10;
	}
	if (heartbeat_interval_ms == 0)
		heartbeat_interval_ms = 500;
	if (heartbeat_interval_ms >= check_interval_ms) {
		pr_warn("%s: heartbeat_interval_ms=%u 不小于 check_interval_ms=%u，钳制为一半\n",
			MODULE_NAME, heartbeat_interval_ms, check_interval_ms);
		heartbeat_interval_ms = check_interval_ms / 2;
	}
	if (stall_threshold == 0)
		stall_threshold = 1;
	if (unresponsive_threshold < stall_threshold)
		unresponsive_threshold = stall_threshold;

	/* 2. 校验 monitor_cpu：越界或未上线则回退 */
	if (monitor_cpu >= nr_cpu_ids || !cpu_online(monitor_cpu)) {
		unsigned int fallback = cpumask_first(cpu_online_mask);

		pr_warn("%s: monitor_cpu=%u 非法（nr_cpu_ids=%u），回退到 cpu%u\n",
			MODULE_NAME, monitor_cpu, nr_cpu_ids, fallback);
		monitor_cpu = fallback;
	}
	ctx->monitor_cpu = monitor_cpu;
	cpumask_clear(&ctx->target_mask);
	ctx->nr_targets = 0;

	/* 3. 分配心跳线程指针数组 */
	ctx->hb_threads = kcalloc(nr_cpu_ids, sizeof(struct task_struct *),
				  GFP_KERNEL);
	if (!ctx->hb_threads)
		return -ENOMEM;

	/* 4. 初始化每核心状态 */
	for_each_possible_cpu(cpu) {
		struct per_core_hb_cpu_state *st;

		st = per_cpu_ptr(&per_core_hb_cpu_states, cpu);
		atomic64_set(&st->heartbeat_counter, 0);
		st->last_seen = 0;
		st->miss_count = 0;
		st->state = PER_CORE_HB_OFFLINE;
	}
	/* 监控核心自身状态标记 */
	WRITE_ONCE(per_cpu_ptr(&per_core_hb_cpu_states,
			       ctx->monitor_cpu)->state, PER_CORE_HB_SELF);

	/* 5. 注册 CPU hotplug 回调（invoke=true：对已 online 核心执行 online 回调，
	 *    即创建各核心心跳线程） */
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "per_core_heartbeat",
				per_core_hb_cpu_online, per_core_hb_cpu_offline);
	if (ret < 0) {
		pr_err("%s: 注册 CPU hotplug 回调失败 (%d)\n", MODULE_NAME, ret);
		goto err_free;
	}
	hotplug_state = ret;

	/* 6. 创建监控线程并绑核到 monitor_cpu */
	if (monitor_enabled) {
		ctx->monitor_thread = kthread_create(per_core_hb_monitor_fn, ctx,
						     "per_core_hb_monitor");
		if (IS_ERR(ctx->monitor_thread)) {
			ret = PTR_ERR(ctx->monitor_thread);
			pr_err("%s: 创建监控线程失败 (%d)\n", MODULE_NAME, ret);
			goto err_hotplug;
		}
		kthread_bind(ctx->monitor_thread, ctx->monitor_cpu);
		wake_up_process(ctx->monitor_thread);
	} else {
		ctx->monitor_thread = NULL;
		pr_info("%s: 加载时未启用监控\n", MODULE_NAME);
	}

	/* 7. 创建 procfs 接口 */
	proc_file = proc_create(PROC_FILENAME, 0444, NULL,
				&per_core_hb_proc_fops);
	if (!proc_file) {
		pr_err("%s: 创建 /proc/%s 失败\n", MODULE_NAME, PROC_FILENAME);
		ret = -ENOMEM;
		goto err_monitor;
	}

	pr_info("%s: 加载成功，监控核心=cpu%u，目标核心数=%u\n",
		MODULE_NAME, ctx->monitor_cpu, ctx->nr_targets);
	return 0;

err_monitor:
	if (ctx->monitor_thread) {
		kthread_stop(ctx->monitor_thread);
		ctx->monitor_thread = NULL;
	}
err_hotplug:
	cpuhp_remove_state(hotplug_state);
err_free:
	kfree(ctx->hb_threads);
	ctx->hb_threads = NULL;
	return ret;
}

/* ========== 模块卸载 ========== */
static void __exit per_core_hb_exit(void)
{
	struct per_core_hb_ctx *ctx = &per_core_hb_ctx;

	remove_proc_entry(PROC_FILENAME, NULL);

	if (ctx->monitor_thread) {
		kthread_stop(ctx->monitor_thread);
		ctx->monitor_thread = NULL;
	}

	/*
	 * cpuhp_remove_state(invoke=true) 会对所有 online 核心执行 offline 回调，
	 * 从而停止各核心心跳线程。
	 */
	cpuhp_remove_state(hotplug_state);

	kfree(ctx->hb_threads);
	ctx->hb_threads = NULL;

	pr_info("%s: 已卸载\n", MODULE_NAME);
}

module_init(per_core_hb_init);
module_exit(per_core_hb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Quantum Board");
MODULE_DESCRIPTION("Per-core thread heartbeat monitor (soft lockup detection)");
MODULE_VERSION("1.0");

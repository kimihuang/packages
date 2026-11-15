# Per-Core Thread Heartbeat 内核模块设计文档（方案 B：绑核线程软心跳）

## 1. 背景与目标

在 16 核 ARM64 SoC 上，方案 A（`ipi_monitor`，IPI 轮询心跳）通过核间中断探测
目标核心是否仍能响应 IPI，覆盖 **L2（硬死锁）** 与 **L4（核心硬件失效）**。

但方案 A 存在天然盲区：**L1 软死锁**（内核态死循环但中断开启）时，目标核心
仍能响应 IPI，方案 A 会误判为正常。方案 B 用于填补这一盲区。

方案 B 采用 **per-core 绑核线程软心跳**：

> 每个核心绑定一个独立的内核线程，线程周期性地自增本核心的心跳计数。
> 监控线程通过检查各核心心跳计数是否持续推进，判断该核心**是否还能被
> 调度器正常调度**，从而发现"软死锁"或"调度饿死"的核心。

### 1.1 覆盖的故障模式

| 故障级别 | 现象 | 方案 A（IPI 心跳） | 方案 B（线程心跳） |
|---------|------|:---:|:---:|
| L1 软死锁 | 内核态死循环但中断开启 | ❌ 误判正常 | ✅ 核心目标 |
| L2 硬死锁 | 关中断/关抢占卡死 | ✅ | ✅（能发现，但不能与 L4 区分） |
| L3 用户态卡死 | 进程死循环但内核调度正常 | ❌ | ✅ 可延伸（绑核进程做心跳） |
| L4 核心硬件失效 | 核心不再取指/执行 | ✅ | ✅（能发现，但不能与 L2 区分） |

> 关键结论：方案 B 回答"这个核还能不能被调度"，方案 A 回答"这个核还能不能
> 响应中断"。二者**正交互补**，组合后能区分 L1 与 L2/L4（见 §6.2）。

### 1.2 非目标（Non-Goals）

- 不检测用户态业务逻辑正确性（业务级心跳由应用层实现，本模块仅提供内核级框架）。
- 不实现自动复位/隔离故障核心（仅"检测 + 上报"，由上层决策）。
- 不修改内核核心代码，保持纯模块化。

---

## 2. 方案概述

- **监控核心（Monitor CPU）**：通过模块参数 `monitor_cpu` 可配置，默认 `CPU0`。
  运行一个绑核的**监控线程**，负责检查所有目标核心的心跳。
- **目标核心（Target CPU）**：除监控核心外的所有 online 核心。每个目标核心
  运行一个**绑核心跳线程**（`kthread_run_on_cpu` 创建并绑定）。
- **心跳动作**：心跳线程周期性地 `atomic64_inc(heartbeat_counter)`（本核心计数）。
- **判定逻辑**：监控线程周期性检查每个目标核心的 `heartbeat_counter` 是否相较
  上一次检查发生推进；连续 N 次未推进判定 `stalled`，连续 M 次判定 `unresponsive`。

### 2.1 为什么使用 `kthread_run_on_cpu` 而非 `kthread_create` + `kthread_bind`

- `kthread_run_on_cpu(threadfn, data, cpu, namefmt)` 是内核提供的
  "创建并绑定到指定 CPU 并唤醒"的一步式封装（`include/linux/kthread.h`），
  内部即 `kthread_create_on_cpu` + `wake_up_process`。
- 线程在首次调度前即绑定到目标核心，无"先跑在错误核心再迁移"的漂移窗口。
- 心跳线程的 `cpus_allowed` 固定为目标核心，保证心跳确实反映**该核心**的调度活性。

### 2.2 心跳载体：计数器 vs 时间戳

| 载体 | 优点 | 缺点 |
|------|------|------|
| 计数器（`atomic64_t` 自增） | 与方案 A 判定逻辑完全一致，无回绕/时钟源问题 | 无绝对时间信息 |
| 时间戳（`ktime_get()`） | 可直观反映"最后活动时间" | 需处理时钟源精度与回绕 |

**采用计数器方案**：与方案 A 复用同一套"`counter != last_seen` 推进判定"框架，
两个模块的判定逻辑、状态枚举、数据结构可高度统一，降低维护成本。

### 2.3 心跳线程的调度优先级权衡

心跳线程的优先级直接决定"什么程度的调度延迟会被判定为失联"：

| 优先级 | 误报风险 | 漏报风险 |
|--------|---------|---------|
| 普通优先级 | 系统高负载时心跳线程被饿死 → **误报** | 低 |
| `SCHED_FIFO`（低 RT，如 prio=1） | 仅被更高优先级 RT 任务饿死 | 低 |

**采用 `SCHED_FIFO` + 最低实时优先级（1）**：
- 只要核心还能调度、且没有更高优先级 RT 任务占满，心跳线程就能运行 → 准确反映
  "该核心调度子系统是否存活"；
- 普通负载（CFS 任务）不会饿死 RT 心跳线程，避免误报；
- 仅当核心真软死锁（关抢占/死循环）或 RT 死锁时才会停更 → 这正是要检测的故障。

---

## 3. 架构设计

### 3.1 架构总览

```
              +------------------------------------------------------+
              |              SoC (16 × ARM64 core)                     |
              |                                                        |
              |   CPUx (Monitor)          CPUy (Target, y != x)         |
              |   x = monitor_cpu 可配置                               |
              |   +------------------+    +------------------+          |
              |   |  monitor_thread  |    |  hb_thread[y]    |          |
              |   |  (kthread_run_on_ |    |  (kthread_run_on_ |        |
              |   |   cpu, SCHED_FIFO)|    |   cpu, SCHED_FIFO)|        |
              |   +--------+---------+    +--------+---------+          |
              |            |                       |                     |
              |            |   读取 per_cpu        |  周期自增本核心       |
              |            |   heartbeat_counter   |  heartbeat_counter  |
              |            |<----------------------+                     |
              |            |   比对是否推进          |                     |
              |            +-----------------------> 判定                |
              +------------------------------------------------------+
                                   |
                                   | 上报 (printk / procfs)
                                   v
                           用户态 / 上层决策
```

### 3.2 数据流

```
[心跳线程 hb_thread[cpu]] --每 heartbeat_interval_ms 唤醒一次-->
        atomic64_inc(per_cpu(heartbeat_counter))

[监控线程] --每 check_interval_ms 唤醒一次--> 遍历目标核心
        |
        +--> cur = atomic64_read(counter)
        +--> cur != last_seen ? 
                 |
                 +-- 推进   --> 标记 OK，清零 miss_count
                 +-- 未推进 --> miss_count++
                                 |
                                 +-- 达到 stall 阈值 --> STALLED
                                 +-- 达到 unresponsive 阈值 --> UNRESPONSIVE
```

---

## 4. 核心数据结构

定义于 `per_core_heartbeat.h`。

### 4.1 每核心心跳状态

```c
struct per_core_hb_cpu_state {
    atomic64_t heartbeat_counter; /* 心跳计数，绑核线程中自增 */
    u64        last_seen;         /* 监控线程最近观察到的 counter 快照 */
    u32        miss_count;        /* 连续未推进的检查周期数 */
    u32        state;             /* 当前状态（枚举见 4.3） */
};
```

- `heartbeat_counter` 由对应核心的绑核线程通过 `atomic64_inc` 自增，监控线程
  `atomic64_read` 读取，跨核心访问无需额外锁。
- 使用 per-cpu 变量存储，避免 cache line 伪共享：

```c
static DEFINE_PER_CPU(struct per_core_hb_cpu_state, per_core_hb_cpu_states);
```

### 4.2 全局监控上下文

```c
struct per_core_hb_ctx {
    struct task_struct *monitor_thread;         /* 监控线程 task_struct */
    struct task_struct **hb_threads;            /* 各核心心跳线程 task_struct 数组 */
    struct cpumask      target_mask;            /* 目标核心掩码（online - {monitor_cpu}） */
    unsigned int        monitor_cpu;            /* 监控核心编号（模块参数，加载时校验） */
    unsigned int        nr_targets;             /* 当前目标核心数量 */
};
```

- `hb_threads` 为按 CPU 编号索引的指针数组（`kcalloc(nr_cpu_ids, ...)` 分配），
  仅目标核心位置非空。

### 4.3 状态枚举

```c
enum per_core_hb_cpu_status {
    PER_CORE_HB_OK           = 0,  /* 正常，心跳持续推进 */
    PER_CORE_HB_STALLED      = 1,  /* 软性失联，连续 miss >= stall_threshold */
    PER_CORE_HB_UNRESPONSIVE = 2,  /* 硬性失联，连续 miss >= unresponsive_threshold */
    PER_CORE_HB_OFFLINE      = 3,  /* 核心已下线，不参与监控 */
    PER_CORE_HB_SELF         = 4,  /* 监控核心自身 */
};
```

---

## 5. 工作流程

### 5.1 心跳线程主循环（目标核心，进程上下文）

```c
static int per_core_hb_thread_fn(void *data)
{
    struct per_core_hb_cpu_state *st;

    st = this_cpu_ptr(&per_core_hb_cpu_states);

    while (!kthread_should_stop()) {
        /* 自增本核心心跳计数，证明"本核心仍可被调度" */
        atomic64_inc(&st->heartbeat_counter);

        /* 睡眠一个心跳周期 */
        schedule_timeout_interruptible(msecs_to_jiffies(heartbeat_interval_ms));
    }

    return 0;
}
```

- 运行于进程上下文，可睡眠，无中断上下文约束。
- 心跳周期 `heartbeat_interval_ms` 应**小于**监控检查周期 `check_interval_ms`，
  保证每个检查周期内至少发生一次心跳。

### 5.2 心跳线程创建与绑核

```c
/* 为每个目标核心创建并绑定心跳线程 */
for_each_cpu(cpu, &ctx->target_mask) {
    t = kthread_run_on_cpu(per_core_hb_thread_fn, NULL, cpu,
                           "hb_per_core.%u");
    ctx->hb_threads[cpu] = t;
    /* 设置 SCHED_FIFO 最低实时优先级（prio=1），避免高负载下误报 */
    sched_set_fifo_low(t);
}
```

> 注：`kthread_run_on_cpu` 的 `namefmt` 格式限制为 `"name.*%u"`，内核会填入
> CPU 编号，生成的线程名形如 `hb_per_core.3`。

### 5.3 监控线程主循环（监控核心，进程上下文）

```c
static int per_core_hb_monitor_fn(void *data)
{
    while (!kthread_should_stop()) {
        for_each_cpu(cpu, &ctx->target_mask) {
            st = per_cpu_ptr(&per_core_hb_cpu_states, cpu);
            cur = atomic64_read(&st->heartbeat_counter);

            if (cur != st->last_seen) {
                /* 心跳推进，正常 */
                new_state = OK;
                miss_count = 0;
            } else {
                miss_count++;
                if (miss_count >= unresponsive_threshold) new_state = UNRESPONSIVE;
                else if (miss_count >= stall_threshold) new_state = STALLED;
                else new_state = old_state;
            }
            /* 状态迁移上报 + 可选 panic，逻辑同方案 A */
            st->last_seen = cur;
        }

        schedule_timeout_interruptible(msecs_to_jiffies(check_interval_ms));
    }
    return 0;
}
```

### 5.4 时序图

```
时间轴 --->

心跳线程(CPU1):  [inc]--[sleep hb]--[inc]--[sleep hb]--[inc]--...
心跳线程(CPU2):  [inc]--[sleep hb]--[inc]--[sleep hb]--[inc]--...
心跳线程(CPU3):  [inc]--[sleep hb]--(软死锁, 不再运行).........

监控线程:       [检查]---[sleep ck]---[检查]---[sleep ck]---[检查]...

判定:    CPU1/CPU2 counter 持续推进 -> OK
         CPU3 counter 停更 -> miss_count++ -> STALLED -> UNRESPONSIVE
```

---

## 6. 故障检测与分级

### 6.1 独立判定

| 状态 | 判定条件 | 含义 | 默认动作 |
|------|---------|------|---------|
| OK | counter 持续推进 | 核心可被正常调度 | 无 |
| STALLED | 连续 `stall_threshold` 次未推进 | 调度可能短暂饥饿 | `pr_warn` 上报 |
| UNRESPONSIVE | 连续 `unresponsive_threshold` 次未推进 | 软死锁/硬死锁/硬件失效 | `pr_err` 上报，可选 panic |
| OFFLINE | `!cpu_online(cpu)` | 核心已下线 | 跳过 |

### 6.2 与方案 A 组合的故障定位

方案 B 单独无法区分 L1（软死锁）与 L2/L4（硬死锁/硬件失效），二者都表现为
"心跳线程停更"。结合方案 A 的 IPI 可达性结果即可定位：

| 方案 A 结果 | 方案 B 结果 | 诊断结论 |
|:---:|:---:|---------|
| OK | OK | 核心正常 |
| OK | STALLED/UNRESPONSIVE | **L1 软死锁**（能响应 IPI 但不能调度） |
| STALLED/UNRESPONSIVE | STALLED/UNRESPONSIVE | **L2/L4**（既不能响应 IPI 也不能调度） |

> 建议：两个模块的 `monitor_cpu`、判定阈值保持一致，并通过统一的上报通道
> （如共用一份状态或由上层聚合）实现交叉定位。

---

## 7. 可配置参数（模块参数）

| 参数 | 类型 | 默认值 | 说明 |
|------|------|-------|------|
| `monitor_cpu` | uint | 0 | 监控线程运行的核心编号，加载时校验，非法回退 |
| `heartbeat_interval_ms` | uint | 500 | 心跳线程自增周期（毫秒），应小于检查周期 |
| `check_interval_ms` | uint | 1000 | 监控检查周期（毫秒） |
| `stall_threshold` | uint | 3 | 判定 STALLED 所需连续 miss 次数 |
| `unresponsive_threshold` | uint | 10 | 判定 UNRESPONSIVE 所需连续 miss 次数 |
| `panic_on_unresponsive` | bool | 0 | UNRESPONSIVE 时是否触发 panic |
| `monitor_enabled` | bool | 1 | 模块加载后是否立即启动监控 |

### 7.1 `monitor_cpu` 校验流程

与方案 A 一致：`monitor_cpu >= nr_cpu_ids` 或 `!cpu_online(monitor_cpu)` 时回退
到第一个 online 核心并告警。

### 7.2 参数约束

- `heartbeat_interval_ms < check_interval_ms`：保证每个检查周期至少一次心跳；
  不满足时钳制 `heartbeat_interval_ms = check_interval_ms / 2`。
- `unresponsive_threshold >= stall_threshold`，`stall_threshold >= 1`。

---

## 8. 接口设计

### 8.1 procfs 状态接口

创建 `/proc/per_core_heartbeat`，`cat` 输出每个核心的状态：

```
CPU   STATE          COUNTER          MISS
0     MONITOR        -                -
1     OK             12345            0
2     OK             12340            0
3     UNRESPONSIVE   5000             12
...
```

- 只读接口（`proc_ops.proc_read` + `seq_file`）。
- 每次读取即时遍历 per-cpu 状态。

### 8.2 日志接口

- 状态迁移时打印（OK→STALLED、STALLED→UNRESPONSIVE、恢复→OK）。
- 使用 `pr_warn` / `pr_err`，遵循内核日志规范。

---

## 9. 并发与安全分析

| 场景 | 保护机制 |
|------|---------|
| 心跳线程与监控线程并发访问 `heartbeat_counter` | `atomic64_t` 原子读写，无锁 |
| 监控线程读取 per-cpu 状态 | `per_cpu()` + `atomic64_read`，安全 |
| 多个心跳线程同时自增 | 各核心操作自身 per-cpu 变量，无共享竞争 |
| 心跳线程生命周期与 hotplug | 见 §10.2，offline 时停止线程并置 OFFLINE |
| 监控核心下线 | 见 §10.3，告警并停止监控 |

### 9.1 死锁风险

- 心跳线程与监控线程均为独立的进程上下文线程，仅通过 per-cpu 原子计数器交互，
  无共享锁，无死锁风险。
- 心跳线程运行在目标核心，若该核心软死锁，仅该核心的心跳线程停更，不影响
  监控线程与其他核心。

---

## 10. 边界情况处理

### 10.1 监控核心自身

监控核心不运行独立心跳线程——监控线程自身运行在监控核心，其存活即监控核心的
心跳证明。若需监控核心自检，可由监控线程直接自增本核心计数并比对。

### 10.2 目标核心 offline/online

- 使用 `cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, ...)` 注册 hotplug 回调：
  - online 回调：创建该核心心跳线程，重置状态为 OK；
  - offline 回调：`kthread_stop` 该核心心跳线程，置状态为 OFFLINE。
- 注意 `kthread_stop` 会唤醒绑定的线程；若线程所在核心正 offline，需保证线程
  在核心下线前被停止（利用 CPUHP 阶段保证）。

### 10.3 监控核心被 offline

- 简单方案：检测到 `monitor_cpu` 下线时停止监控并告警（不自动迁移，避免复杂度）。
- 更健壮方案（可选）：迁移监控线程到新的 `monitor_cpu` 并重建 `target_mask`。

### 10.4 模块卸载

- 停止监控线程（`kthread_stop`）。
- 停止所有目标核心的心跳线程。
- 释放 `hb_threads` 数组（`kfree`）。
- 移除 procfs 节点。
- 清理 per-cpu 状态。

---

## 11. 文件结构

```
per-core-thread_monitor/
├── DESIGN.md                  # 本文档
├── per_core_heartbeat.h       # 数据结构、状态枚举、接口声明
├── per_core_heartbeat.c       # 模块实现（心跳线程/监控线程/procfs/hotplug）
├── Kbuild                     # obj-m += per_core_heartbeat.o
├── Kconfig                    # Buildroot 集成配置项
├── COPYING                    # GPL-2.0 许可证
└── README.md                  # 使用说明
```

---

## 12. 与方案 A 及内核原生机制的关系

| 机制 | 覆盖故障 | 与本方案的关系 |
|------|---------|---------------|
| 方案 A（IPI 心跳） | L2、L4 | 互补：区分 L1 与 L2/L4（见 §6.2） |
| soft lockup detector（`kernel/watchdog.c`） | L1 | 内核原生软死锁检测，本方案的自研版本 |
| hard lockup detector（NMI） | L2 | 内核原生硬死锁检测，与方案 A 互补 |
| RCU CPU stall（`kernel/rcu/tree_stall.h`） | L1/L2 | 内核原生"免费"健康信号 |

> 定位：方案 B 是**精确到单核的调度活性检测**，与方案 A（IPI 可达性检测）及
> 内核原生 watchdog 共同构成多层健康监测体系。

---

## 13. 测试计划

1. **正常加载/卸载**：`insmod` / `rmmod` 无崩溃，procfs 正常创建/移除，
   心跳线程随模块创建/销毁。
2. **全核心正常**：`cat /proc/per_core_heartbeat` 显示所有核心 OK，counter 持续推进。
3. **模拟软死锁**：在目标核心上执行 `preempt_disable()` + 死循环（或持有自旋锁
   死循环），预期该核心被标记 STALLED → UNRESPONSIVE，其余核心 OK。
4. **高负载误报测试**：运行 CPU 密集任务占满目标核心，验证 SCHED_FIFO 心跳线程
   仍能运行（不误报）。
5. **参数边界**：`heartbeat_interval_ms=0`、`stall_threshold=0` 等非法值被钳制。
6. **CPU hotplug**：offline/online 目标核心，心跳线程正确停止/重建，状态迁移正确。
7. **与方案 A 联动**：同时加载两个模块，构造 L1/L2 故障，验证 §6.2 交叉定位结论。

---

## 14. 交付清单

- [ ] `per_core_heartbeat.h`：数据结构与接口声明
- [ ] `per_core_heartbeat.c`：完整实现
- [ ] `Kbuild` / `Kconfig` / `COPYING` / `README.md`
- [ ] 交叉编译通过（ARM64 目标）

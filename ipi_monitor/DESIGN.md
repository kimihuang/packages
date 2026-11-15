# IPI Monitor 内核模块设计文档（方案 A'：环状双向互监 + 每核 hrtimer 自心跳）

## 1. 背景与目标

在 16 核 ARM64 SoC 上，需要一个机制检测每个 CPU 核心是否"正常运行"。

最初的方案 A 采用 **IPI 轮询心跳**：由一个监控核周期性向其余核心发送 IPI，
目标核心在 IPI 回调中自增心跳计数，监控核心通过比对计数是否推进判断目标是否失联。

该方案存在两个结构性缺陷：

1. **监控线程依赖调度**：发送 IPI 与比对都由绑定在 `monitor_cpu` 上的内核线程驱动，
   通过 `schedule_timeout_interruptible()` 周期性唤醒。一旦监控核被高优先级 RT 任务
   长期占满或线程被饿死，整个监控机制静默失效——watchdog 自己成了最脆弱的一环。
2. **单监控核单点**：只有 `monitor_cpu` 一个监控者，它自身失联（或硬件失效）无人监测。

本方案（方案 A'）针对上述缺陷做两处演进：

- **心跳载体改为"每核硬中断 hrtimer"**：每个 core 运行一个固定在本核的硬中断
  hrtimer，回调里 `atomic64_inc` 自己的心跳计数。心跳信号不再依赖线程调度，也
  不再需要 IPI。
- **监控拓扑改为"环状双向互监"**：每个 core 监控环上的前驱核与后继核，每个 core
  因此被两个邻居监控，消除单监控核单点。

### 1.1 覆盖的故障模式

| 故障级别 | 现象 | 能否响应中断 | 本方案是否覆盖 |
|---------|------|:---:|:---:|
| L1 软死锁 | 内核态死循环但中断开启 | ✅ 能 | ❌（hrtimer 仍会触发，需配合方案 B 软心跳） |
| L2 硬死锁 | 关中断/关抢占卡死 | ❌ 不能 | ✅ 核心目标 |
| L4 核心硬件失效 | 核心不再取指/执行 | ❌ 不能 | ✅ 核心目标 |

> 说明：ARM64 上 SGI（核间中断）与 PPI（私有定时器中断）同属 IRQ 类，统一受
> `PSTATE.I`（DAIF.I）全局屏蔽位控制，没有"只屏蔽 SGI 不屏蔽 PPI"的核级开关。
> 因此"关中断卡死"会同时阻断 IPI 与本核 hrtimer，二者对 L2/L4 的检测能力等效。
> 差异仅在于 IPI 额外验证了 GIC 的 SGI 分发可达性——若需覆盖该罕见故障，可保留
> 一个低频 IPI 自检，本方案默认不做。

### 1.2 非目标（Non-Goals）

- 不检测 L1 软死锁与 L3 用户态卡死（由内核原生 soft lockup detector 或方案 B
  `per-core-thread_monitor` 绑核线程软心跳补充）。
- 不实现自动复位/隔离故障核心（本模块仅"检测 + 上报"）。
- 不修改内核核心代码，保持纯模块化。

---

## 2. 方案概述

- **心跳生成**：每个 online 核心运行一个 `hrtimer`（`HRTIMER_MODE_*_PINNED_HARD`，
  硬中断 + 固定核），回调在硬中断上下文执行 `atomic64_inc(heartbeat_counter)`。
- **监控关系**：按 core 编号形成环，每个 core 监控**前驱**与**后继**两个邻居
  （仅本核 online 时为 0 个，仅 2 核 online 时前驱==后继，去重为 1 个）。
- **判定逻辑**：监控者在自己的 hrtimer 回调里读取被监控核的 `heartbeat_counter`，
  与本地快照比对；连续 N 次未推进判定 `STALLED`，连续 M 次判定 `UNRESPONSIVE`。
- **状态上报**：硬中断里避免 printk，状态迁移通过 `schedule_work()` 延迟到
  workqueue（进程上下文）打印；`UNRESPONSIVE` 且 `panic_on_unresponsive` 时在
  硬中断里直接 `panic()`。
- **hotplug**：online 时在目标核上启动其 hrtimer；offline 时取消；监控关系每周期
  由 `cpu_online_mask` 动态计算，peer 身份变化时自动重置快照。

### 2.1 为什么不再需要 IPI

IPI 检测的本质是"目标核能否响应一次中断并执行一小段代码"。各核自己的硬中断
hrtimer 回调里自增计数，测的是**完全一样的东西**（这次中断是本核私有定时器中断
PPI，而非跨核 SGI）。L2/L4 场景下两者都被同样阻断，检测能力等效；而自心跳还顺带
消除了"发送者依赖调度"这一失效点。

### 2.2 为什么用硬中断 hrtimer 而非定时器轮（timer_list）

- `timer_list`（`TIMER_SOFTIRQ`）运行于 softirq 上下文，极端负载下 softirq 可能被
  推迟到 ksoftirqd，仍有一定调度依赖。
- 硬中断 hrtimer（`HRTIMER_MODE_*_HARD`）由 arch timer 的 PPI 直接驱动，硬中断能
  抢占除关中断窗口外的几乎一切，最能保证"失联场景下 watchdog 仍运转"。
- 前提：`atomic64_inc` / `atomic64_read` / `smp_processor_id()` 在硬中断上下文均安全；
  本回调不睡眠、不阻塞、不 printk（除 `panic()`）。

---

## 3. 架构设计

### 3.1 架构总览

```
                     SoC (16 × ARM64 core)，每个 core 一个 hrtimer（硬中断，固定本核）

        core0            core1            core2          ...         core15
        +--------+       +--------+       +--------+                 +--------+
        |hrtimer |       |hrtimer |       |hrtimer |                 |hrtimer |
        | 自心跳  |       | 自心跳  |       | 自心跳  |                 | 自心跳  |
        | 监控    |------>| 监控    |------>| 监控    |---- ... ----->| 监控    |
        | 15 & 1 |<------| 0  & 2 |<------| 1  & 3 |<---- ... ------| 14 & 0 |
        +--------+       +--------+       +--------+                 +--------+

        监控关系（环状双向）：
           core N 监控 prev(N) 与 next(N)
           core N 被 prev(N) 与 next(N) 监控

        每个 core 的 hrtimer 回调：
           atomic64_inc(自己 counter)         // 自心跳
           read prev/next 的 counter 并比对    // 监控邻居
```

### 3.2 数据流

```
[core N 的 hrtimer 回调，硬中断]
    |
    +--> atomic64_inc(per_cpu(N).heartbeat_counter)   // 自证"我还活着"
    |
    +--> 对 peer in {prev(N), next(N)}：
    |       cur  = atomic64_read(per_cpu(peer).heartbeat_counter)
    |       与 per_cpu(N).peer_last_seen[slot] 比对
    |         +-- 推进   --> peer_state=OK, peer_miss=0
    |         +-- 未推进 --> peer_miss++，达阈值 --> STALLED/UNRESPONSIVE
    |       peer_last_seen[slot] = cur
    |
    +--> 状态迁移 --> schedule_work(上报) / panic
```

---

## 4. 核心数据结构

定义于 `ipi_monitor.h`。

```c
#define IPI_MONITOR_NR_PEERS 2

struct ipi_monitor_cpu_state {
    atomic64_t heartbeat_counter;   /* 本核自心跳，本核 hrtimer 自增，他核只读 */

    int  peer_cpu[2];               /* 本核监控的前驱/后继编号，-1 无效 */
    u64  peer_last_seen[2];         /* 本核对 peer counter 的快照 */
    u32  peer_miss[2];              /* 本核判定的 peer 连续 miss 次数 */
    u32  peer_state[2];             /* 本核判定的 peer 状态 */

    u32  reported_state;            /* 上报 work 的聚合状态缓存（去重） */
};
```

**并发模型**（关键设计）：

- `heartbeat_counter`：`atomic64_t`，本核 hrtimer 写、邻居 hrtimer 读，跨核无锁。
- `peer_*` 数组：只有"本核 hrtimer"这一个写者，跨核只读（procfs 聚合用 `READ_ONCE`），
  无锁、无竞争。
- `reported_state`：只有上报 work（进程上下文、单 work 项串行）写，`READ/WRITE_ONCE`
  保证可见性。

这避免了"两个监控者同时写目标核状态"的竞争：**每个监控者只写自己的 peer 观测，
从不写目标核的数据**。目标核的对外状态由 procfs/上报时聚合得出。

### 4.1 状态枚举

```c
enum ipi_monitor_cpu_status {
    IPI_MONITOR_OK           = 0,  /* 心跳持续推进 */
    IPI_MONITOR_STALLED      = 1,  /* 连续 miss >= stall_threshold */
    IPI_MONITOR_UNRESPONSIVE = 2,  /* 连续 miss >= unresponsive_threshold */
    IPI_MONITOR_OFFLINE      = 3,  /* 核心已下线 */
};
```

---

## 5. 工作流程

### 5.1 hrtimer 回调（`ipi_monitor_tick`，硬中断上下文）

每个检查周期完成三件事：

1. **自心跳**：`atomic64_inc(&this_cpu->heartbeat_counter)`。
2. **计算监控对象**：`prev_online(me)` 与 `next_online(me)`（环上邻居，动态计算）。
3. **逐个监控**：对每个 peer 读 counter、比对快照、更新 `peer_miss`/`peer_state`。
   - 若 `peer_cpu[slot]` 与上次不同（hotplug 导致），重置该 slot 快照并跳过本轮比对。
4. **续期**：`hrtimer_forward_now()` + `HRTIMER_RESTART`。

约束：

- 硬中断上下文：不睡眠、不阻塞、不 printk（迁移日志延迟到 workqueue）。
- 计数推进用 `!=` 判定（非 `>`），逻辑稳健。

### 5.2 上报（`ipi_monitor_report_workfn`，进程上下文）

- 由 hrtimer 回调在状态迁移时 `schedule_work()` 触发（`schedule_work` 原子上下文安全，
  多次调度自动合并）。
- work 遍历 online 核，`ipi_monitor_aggregate()` 汇总（取最差状态/最大 miss），
  与 `reported_state` 比对后打印迁移日志（`pr_warn`）。
- 检测与上报彻底解耦：即便 workqueue 被饿死，检测（自心跳 + 比对）照常进行。

### 5.3 procfs 状态接口

`/proc/ipi_monitor`（只读）输出每个 core 的聚合状态：

```
CPU   STATE          COUNTER          MISS
0     OK             12345            0
1     OK             12340            0
2     UNRESPONSIVE   5000             12
...
interval=1000ms stall=3 unresponsive=10 panic=0 online=16
```

- `STATE`：该 core 被所有监控者判定的最差状态。
- `COUNTER`：该 core 自身的 `heartbeat_counter`。
- `MISS`：所有监控者对该 core 判定的最大 miss 次数。

---

## 6. 故障检测与分级

| 状态 | 判定条件 | 含义 | 默认动作 |
|------|---------|------|---------|
| OK | counter 持续推进 | 核心正常响应中断 | 无 |
| STALLED | 连续 `stall_threshold` 次未推进 | 可能短暂卡顿/负载极高 | `pr_warn` 上报 |
| UNRESPONSIVE | 连续 `unresponsive_threshold` 次未推进 | 大概率硬死锁/硬件失效 | `pr_err` 上报，可选 panic |
| OFFLINE | `!cpu_online(cpu)` | 核心已下线 | 跳过 |

- 恢复推进后 `peer_miss` 立即清零，避免历史累计误判。

---

## 7. 可配置参数（模块参数）

| 参数 | 类型 | 默认值 | 说明 |
|------|------|-------|------|
| `check_interval_ms` | uint | 1000 | 检查周期（毫秒），最小 10 |
| `stall_threshold` | uint | 3 | 判定 STALLED 所需连续 miss 次数 |
| `unresponsive_threshold` | uint | 10 | 判定 UNRESPONSIVE 所需连续 miss 次数 |
| `panic_on_unresponsive` | bool | 0 | UNRESPONSIVE 时是否触发 panic |
| `monitor_enabled` | bool | 1 | 加载后是否立即启动监控 |

> 注：原 `monitor_cpu` 参数已移除（本方案无单监控核概念）。

---

## 8. 并发与安全分析

| 场景 | 保护机制 |
|------|---------|
| 邻居读本核 `heartbeat_counter` | `atomic64_t` 原子读写，无锁 |
| 本核 hrtimer 写 `peer_*` | 单写者（本核），跨核只读 + `READ_ONCE` |
| 多个监控者判定同一核 | 各自写自己的 peer 观测，procfs/上报时聚合取最差，无竞争 |
| peer 身份变化（hotplug） | 每周期动态计算 + 身份变化重置快照 |
| 硬中断里 printk | 避免，延迟到 workqueue |
| 计数器回绕 | `!=` 判定，且 u64 实际不回绕 |
| 定时器续期漂移 | `hrtimer_forward_now` 按周期对齐，无累积漂移 |

### 8.1 死锁风险

- hrtimer 回调仅做原子操作与 `schedule_work`（无锁），无死锁风险。
- `panic()` 在硬中断上下文调用是安全的（内核 watchdog/nmi 同样在硬中断 panic）。

---

## 9. hotplug 维护

### 9.1 核心上线（`ipi_monitor_cpu_online`）

- 重置本核状态（`heartbeat_counter=0`、peer 快照清空）。
- **在本核上** `hrtimer_init` + `hrtimer_start`（PINNED 模式必须在目标核启动）。
- 依赖 `cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, invoke=true)`：AP 状态回调由 cpuhp
  框架保证运行在目标核（`cpuhp_invoke_ap_callback` → 目标核 hotplug 线程，
  `kernel/cpu.c`）。`invoke=true` 使模块加载时对已 online 核也执行一次 online 回调。

### 9.2 核心下线（`ipi_monitor_cpu_offline`）

- `hrtimer_cancel(timer)` 取消本核 PINNED 定时器（PINNED 定时器不随 hotplug 迁移，
  必须显式取消；定时器已在上线回调初始化，取消安全）。

### 9.3 监控关系的动态维护

- 不维护显式邻接表；每周期由 `cpu_online_mask` 计算 `prev_online()` / `next_online()`。
- 核心下线后，其邻居下一周期自动指向新的前驱/后继；`peer_cpu[slot]` 身份变化触发
  快照重置并跳过本轮比对，避免"新 peer 首次快照"被误判为 miss。
- 核心重新上线后，其自身状态与 hrtimer 重建，邻居的快照通过身份变化自动重置。

### 9.4 模块卸载（`ipi_monitor_exit`）

- `remove_proc_entry` 移除 procfs 节点。
- `cpuhp_remove_state` 触发各 online 核的 offline 回调，取消所有 hrtimer。
- `flush_work` 等待上报 work 完成。

---

## 10. 边界情况处理

### 10.1 仅 1 核 online

`prev_online == next_online == -1`，无 peer 可监控，该核只做自心跳。

### 10.2 仅 2 核 online

`prev_online == next_online == 对方`，去重后只监控 1 个 peer（不重复判定）。

### 10.3 相邻两核同时失联

core5 与 core6 同时失联：core7 监控 core6（前驱），core4 监控 core5（后继），
二者均可被独立检测；core5/core6 互为监控者的那一路失效，不影响其余核的监控链。

### 10.4 首个 tick 误判

新 peer 首次出现时（含模块加载后首个周期），快照重置并跳过本轮比对，从下一周期
才开始计数，避免把"尚未产生心跳"误判为 miss。

---

## 11. 文件结构

```
ipi_monitor/
├── DESIGN.md          # 本文档
├── ipi_monitor.h      # 数据结构、状态枚举、宏
├── ipi_monitor.c      # 模块实现（hrtimer 回调、hotplug、procfs、上报 work）
├── Kbuild             # obj-m += ipi_monitor.o
├── Kconfig            # Buildroot 集成配置项
├── COPYING            # GPL-2.0 许可证
└── README.md          # 使用说明
```

---

## 12. 与内核原生机制的关系

| 机制 | 内核位置 | 与本方案的关系 |
|------|---------|---------------|
| hard lockup detector | `kernel/watchdog.c` + perf/hrtimer | 概念相近（每核硬中断自检），本方案额外提供交叉互监/冗余、procfs 集中展示、可配阈值与 panic |
| soft lockup detector | `kernel/watchdog.c` | 补充检测 L1 软死锁 |
| RCU CPU stall | `kernel/rcu/tree_stall.h` | 内核原生"免费"健康信号 |
| 方案 B `per-core-thread_monitor` | 绑核线程软心跳 | 补充检测 L1（本方案测不出的调度饥饿） |

---

## 13. 测试计划

1. **正常加载/卸载**：`insmod` / `rmmod` 无崩溃，procfs 正常创建/移除。
2. **全核心正常**：`cat /proc/ipi_monitor` 显示所有核 OK，counter 持续推进。
3. **模拟目标核心硬死锁**：在目标核心上执行关中断死循环，预期该核被标记
   STALLED → UNRESPONSIVE，其余核仍 OK。
4. **参数边界**：`check_interval_ms=0`、`stall_threshold=0` 等非法值被钳制。
5. **CPU hotplug**：
   - offline 目标核，其状态显示 OFFLINE，邻居监控关系自动调整；
   - 重新 online，状态恢复 OK 并重新纳入监控。
6. **冗余验证**：使两个相邻核之一失联，确认另一核仍能被其另一邻居检测。
7. **计数器回绕**：构造（或单元测试模拟）计数回绕，推进判定仍正确。

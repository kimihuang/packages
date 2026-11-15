# Per-Core Thread Heartbeat Kernel Module

## 描述

基于**绑核线程软心跳**的多核调度活性监测内核模块（方案 B）。

每个目标核心绑定一个独立的内核线程，线程周期性地自增本核心的心跳计数。
监控线程通过检查计数是否持续推进，判断核心是否仍能被调度器正常调度，
从而发现"软死锁"或"调度饿死"的核心。

覆盖故障模式：

- L1 软死锁（内核态死循环但中断开启，方案 A 无法检测）
- 辅助区分 L2/L4（与方案 A 组合交叉定位，见 DESIGN.md §6.2）

## 功能

- 监控核心可配置（`monitor_cpu` 模块参数，加载时校验）
- 每目标核心独立心跳线程（`kthread_run_on_cpu` + `SCHED_FIFO` 最低优先级）
- 三级故障分级：OK / STALLED / UNRESPONSIVE
- CPU hotplug 支持（online 创建心跳线程 / offline 停止）
- procfs 状态接口 `/proc/per_core_heartbeat`
- 可选 `panic_on_unresponsive`

## 使用

### 加载模块

```bash
insmod per_core_heartbeat.ko                        # 默认监控核心 CPU0
insmod per_core_heartbeat.ko monitor_cpu=3          # 指定监控核心为 CPU3
insmod per_core_heartbeat.ko heartbeat_interval_ms=500 \
    check_interval_ms=1000 stall_threshold=3 unresponsive_threshold=10
```

### 查看状态

```bash
cat /proc/per_core_heartbeat
```

输出示例：

```
CPU   STATE          COUNTER          MISS
0     MONITOR        -                -
1     OK             12345            0
2     OK             12340            0
3     UNRESPONSIVE   5000             12
```

### 卸载模块

```bash
rmmod per_core_heartbeat
```

## 模块参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| `monitor_cpu` | 0 | 监控线程运行的核心编号，非法回退第一个 online 核心 |
| `heartbeat_interval_ms` | 500 | 心跳线程自增周期（毫秒），应小于检查周期 |
| `check_interval_ms` | 1000 | 监控检查周期（毫秒），最小 10 |
| `stall_threshold` | 3 | 判定 STALLED 所需连续 miss 次数 |
| `unresponsive_threshold` | 10 | 判定 UNRESPONSIVE 所需连续 miss 次数 |
| `panic_on_unresponsive` | 0 | UNRESPONSIVE 时是否触发 panic |
| `monitor_enabled` | 1 | 加载后是否立即启动监控 |

## 与方案 A 联动

本模块（方案 B）与 `ipi_monitor`（方案 A）正交互补：

| 方案 A | 方案 B | 诊断结论 |
|:---:|:---:|---------|
| OK | OK | 核心正常 |
| OK | STALLED/UNRESPONSIVE | L1 软死锁 |
| STALLED/UNRESPONSIVE | STALLED/UNRESPONSIVE | L2/L4 |

## 编译

### Buildroot 集成

在 Buildroot menuconfig 中启用：

```
Board options -> Quantum Board Packages -> per-core-thread_monitor
```

### 手动交叉编译

```bash
make -C <kernel_src> ARCH=arm64 \
     CROSS_COMPILE=aarch64-none-linux-gnu- \
     M=$PWD modules
```

## 设计文档

详见 [DESIGN.md](DESIGN.md)。

## 许可证

GPL-2.0

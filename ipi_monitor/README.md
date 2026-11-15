# IPI Monitor Kernel Module

## 描述

基于**每核硬中断 hrtimer 自心跳 + 环状双向互监**的多核健康监测内核模块。

每个 online 核心运行一个固定在本核的硬中断 hrtimer，回调里自增自己的心跳计数；
同时每个核心监控环上的前驱核与后继核，通过比对它们的计数是否持续推进，检测
"失联"或"卡死"的核心。每个核心因此被两个邻居监控，无单点。

覆盖故障模式：

- L2 硬死锁（关中断/关抢占卡死，无法响应中断）
- L4 核心硬件失效（核心不再取指/执行）

## 功能

- 每核硬中断 hrtimer 自心跳（不依赖线程调度，不使用 IPI）
- 环状双向互监（每核被前驱 + 后继监控）
- 三级故障分级：OK / STALLED / UNRESPONSIVE
- CPU hotplug 支持（online/offline 自动启动/停止定时器并调整监控关系）
- procfs 状态接口 `/proc/ipi_monitor`
- 可选 `panic_on_unresponsive`

## 使用

### 加载模块

```bash
insmod ipi_monitor.ko                                # 默认周期 1000ms
insmod ipi_monitor.ko check_interval_ms=500 \
    stall_threshold=3 unresponsive_threshold=10 panic_on_unresponsive=1
```

### 查看状态

```bash
cat /proc/ipi_monitor
```

输出示例：

```
CPU   STATE          COUNTER          MISS
0     OK             12345            0
1     OK             12340            0
2     UNRESPONSIVE   5000             12
3     OFFLINE        0                0
...
```

### 卸载模块

```bash
rmmod ipi_monitor
```

## 模块参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| `check_interval_ms` | 1000 | 心跳检查周期（毫秒），最小 10 |
| `stall_threshold` | 3 | 判定 STALLED 所需连续 miss 次数 |
| `unresponsive_threshold` | 10 | 判定 UNRESPONSIVE 所需连续 miss 次数 |
| `panic_on_unresponsive` | 0 | UNRESPONSIVE 时是否触发 panic |
| `monitor_enabled` | 1 | 加载后是否立即启动监控 |

## 编译

### Buildroot 集成

在 Buildroot menuconfig 中启用：

```
Board options -> Quantum Board Packages -> ipi-monitor
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

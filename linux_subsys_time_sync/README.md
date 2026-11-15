# Linux Subsystem Time Sync Kernel Module

## 描述

跨核时间同步锚点对广播模块（Linux 侧）。

在 Linux（大核）侧生成时间同步锚点对 `(cntvct_snap, wall_snap)`，写入共享内存
供 RT-Thread（小核）读取，用于把两核日志时间戳对齐到同一墙钟时间轴。

时间来源：

- 计数器：ARM 架构定时器系统计数器 `CNTVCT_EL0`（`arch_timer_read_counter`）
- 墙钟：`CLOCK_REALTIME`（`ktime_get_real_ns`，经 NTP 校准）
- 频率：`CNTFRQ_EL0`（`arch_timer_get_cntfrq`）

## 原理

三明治夹逼生成一致的锚点对：

```
c1 = read_cntvct();  w = ktime_get_real_ns();  c2 = read_cntvct();
cntvct_snap = c1 + (c2 - c1) / 2
```

RT-Thread 侧换算：

```
wall_now = wall_snap + (cntvct_now - cntvct_snap) * 1e9 / freq
```

## 使用

### 加载模块

```bash
# 基础用法：只用 CNTFRQ_EL0 频率，不写共享内存（仅 procfs 调试）
insmod linux_subsys_time_sync.ko

# 完整用法：写共享内存（物理地址 0x80000000），每 500ms 广播一次
insmod linux_subsys_time_sync.ko \
    shared_mem_phys=0x80000000 \
    broadcast_interval_ms=500

# 指定频率覆盖（DT clock-frequency 与 CNTFRQ_EL0 不一致时）
insmod linux_subsys_time_sync.ko \
    shared_mem_phys=0x80000000 freq_override=24000000
```

### 查看状态

```bash
cat /proc/linux_subsys_time_sync
```

### 卸载

```bash
rmmod linux_subsys_time_sync
```

## 模块参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| `shared_mem_phys` | 0 | 共享内存物理地址（reserved memory），0=禁用广播 |
| `shared_mem_size` | 结构体大小 | 共享内存大小 |
| `broadcast_interval_ms` | 1000 | 周期广播间隔（毫秒），0=仅初始化广播一次 |
| `freq_override` | 0 | 频率覆盖（Hz），0=从 CNTFRQ_EL0 自动获取 |

## 共享内存布局（RT-Thread 侧读取）

```c
struct time_sync_anchor {
    u64 magic;          /* TIME_SYNC_MAGIC */
    u64 seq;            /* 奇数=更新中，偶数=稳定 */
    u64 cntvct_snap;    /* CNTVCT 快照 */
    u64 wall_snap;      /* 墙钟快照（纳秒） */
    u32 freq;           /* 频率 Hz */
    u32 reserved;
};
```

读取协议（seqlock）：

```c
do {
    s1 = seq;
    cntvct = cntvct_snap;
    wall   = wall_snap;
    freq   = freq;
    s2 = seq;
} while (s1 != s2 || (s1 & 1));
```

## 注意事项

1. **NTP 步进自动处理**：周期广播会自动把 NTP 步进后的新墙钟基准传播出去，
   无需额外 hook。
2. **频率一致性**：若 DT `clock-frequency` 与 `CNTFRQ_EL0` 不一致，需用
   `freq_override` 指定与 Linux timekeeping 相同的频率，否则两核换算会偏差。
3. **共享内存一致性**：默认假设共享内存是 cache-coherent 的；若小核通过
   非一致区域访问，需在平台层加 cache flush（本模块未处理）。
4. **无 hypervisor 假设**：使用 `CNTVCT_EL0`（虚拟计数器）。若存在 hypervisor
   且两核 `CNTVOFF_EL2` 不同，需改用 `CNTPCT_EL0`（物理计数器）。

## 许可证

GPL-2.0

# Time Sync - RT-Thread 侧跨核时间同步锚点对读取组件

## 描述

读取 Linux（大核）通过共享内存广播的时间同步锚点对 `(cntvct_snap, wall_snap)`，
结合本核读取的 ARM 架构定时器系统计数器 `CNTVCT_EL0`，换算成与 Linux 对齐的
墙钟时间，用于把两核日志时间戳对齐到同一墙钟时间轴。

## 原理

```
wall_now = wall_snap + (cntvct_now - cntvct_snap) * 1e9 / freq
```

- 两核读同一个物理计数器 `CNTVCT`，增量天然一致，无 tick 漂移。
- 用 seqlock（seq 奇偶）保证读锚点时不被 Linux 侧写入撕裂。
- 用 `dmb ish` 内存屏障保证跨核读序。

## 文件

| 文件 | 说明 |
|------|------|
| `time_sync.h` | API + 共享内存锚点结构 |
| `time_sync.c` | 实现：读锚点、读 CNTVCT、算墙钟 |
| `SConscript` | RT-Thread 构建集成 |

## 集成到 RT-Thread

1. 把本目录复制到 RT-Thread 源码树（如 `packages/time_sync/` 或 `components/time_sync/`）。
2. 在 `rtconfig.h` 中定义：

```c
#define TIME_SYNC_SHARED_MEM_ADDR  0x80000000  /* 共享内存虚拟地址（BSP 已映射 reserved memory） */
#define PKG_USING_TIME_SYNC                      /* 使能本组件 */
```

3. 确保共享内存地址已由 BSP 映射（reserved memory 区域），且与 Linux 侧模块参数
   `shared_mem_phys` 对应同一物理内存。

## API

```c
rt_err_t     time_sync_init(void);                 /* 校验共享内存锚点 */
rt_uint64_t  time_sync_get_wallclock_ns(void);     /* 墙钟纳秒（自 epoch） */
rt_uint64_t  time_sync_get_wallclock_us(void);     /* 墙钟微秒（自 epoch） */
```

## 与 ulog 集成（输出墙钟时间戳）

RT-Thread ulog 已支持 `ULOG_TIME_USING_TIMESTAMP` + `gettimeofday()` 输出
"MM-DD HH:MM:SS.mmm" 格式墙钟时间。本组件提供纳秒/微秒接口，接入方式：

### 方式 A（推荐）：修改 libc 的 gettimeofday 回退到本组件

在 `components/libc/compilers/common/ctime.c` 的 `gettimeofday()` 中，RTC 读取
失败后回退到本组件：

```c
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    ...
#ifdef RT_USING_RTC
    if (读 RTC 成功) return 0;
#endif
    /* 回退到跨核时间同步锚点 */
    {
        rt_uint64_t ns = time_sync_get_wallclock_ns();
        if (ns != 0)
        {
            tv->tv_sec  = (time_t)(ns / 1000000000ULL);
            tv->tv_usec = (suseconds_t)((ns % 1000000000ULL) / 1000ULL);
            return 0;
        }
    }
    rt_set_errno(EINVAL);
    return -1;
}
```

### 方式 B：在 ulog 时间戳处直接调用

修改 `components/utilities/ulog/ulog.c` 的 `ULOG_TIME_USING_TIMESTAMP` 分支，
用 `time_sync_get_wallclock_us()` 计算秒 + 微秒后格式化。

## 编译验证

本组件仅依赖 `<rtthread.h>`，不依赖 libc 的 `sys/time.h`，编译干净：

```bash
# 用 aarch64 交叉编译器 + RT-Thread 头文件路径 + rtconfig.h 编译检查
aarch64-none-linux-gnu-gcc -c time_sync.c -Wall -Wextra \
    -I <rtthread>/include \
    -I <rtthread>/libcpu/aarch64/common/include \
    -I <rtthread>/components/finsh \
    -I <rtconfig.h 所在目录>
```

> 说明：`rtconfig.h` 按 BSP 复制一份到独立目录再 `-I` 引入（如 `bsp/phytium/aarch64/rtconfig.h`）。

## 注意事项

1. **计数器一致性**：本组件读 `CNTVCT_EL0`（虚拟计数器），与 Linux 侧
   `arch_timer_read_counter()` 默认一致。若有 hypervisor 且 `CNTVOFF` 不同，
   需两核统一改用 `CNTPCT_EL0`（物理计数器）。
2. **频率一致性**：本组件直接用锚点里的 `freq`（来自 Linux 侧），不独立读
   `CNTFRQ_EL0`，保证两核换算一致。
3. **共享内存一致性**：默认假设 cache-coherent；否则需平台层 cache flush。
4. **NTP 步进**：Linux 侧周期广播 + NTP 步进重广播，本组件每次读取都用最新
   锚点，自动跟随墙钟修正。

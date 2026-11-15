# ulog 墙钟时间戳接入示例

演示如何把 `time_sync` 组件换算出的墙钟时间接入 ulog，让 RT-Thread 的 ulog
日志时间戳从 tick 变成 `YYYY-MM-DD HH:MM:SS.mmm` 墙钟格式，与 Linux（大核）
经 NTP 校准的日志时间戳对齐。

## 文件

| 文件 | 说明 |
|------|------|
| `ulog_timestamp_demo.c` | 自包含示例：格式化 + 演示 |
| `gettimeofday_time_sync.patch` | ctime.c 的 gettimeofday 回退补丁 |
| `SConscript` | RT-Thread 构建集成 |
| `README.md` | 本文档 |

## 快速上手

1. 集成 `time_sync` 组件（见上级目录 README），配置 `TIME_SYNC_SHARED_MEM_ADDR`。
2. 使能本示例：`#define PKG_USING_ULOG_TIME_SYNC_DEMO`。
3. 运行后（开机自动或 msh 命令 `time_sync_ulog_demo`）输出（示例为 UTC 时间）：

```
[ulog_demo] wall clock = 2026-09-16 11:23:23.409
[ulog_demo] wall clock ns = 1789557803409600000
[ulog_demo] sec=1789557803 usec=409600
```

## 让 ulog 真正输出墙钟时间戳（接入方式）

ulog 的时间戳有两条路径（`components/utilities/ulog/ulog.c` 的 `ULOG_OUTPUT_TIME` 分支）：

- 定义了 `ULOG_TIME_USING_TIMESTAMP` → 调用 `gettimeofday()` 输出 `MM-DD HH:MM:SS[.mmm]`
- 未定义 → 调用 `rt_tick_get()` 输出 `[tick]`

### 方式 A（推荐）：应用 gettimeofday 补丁

已提供现成补丁 `gettimeofday_time_sync.patch`，在 RT-Thread 源码根目录执行：

```bash
git apply <path>/gettimeofday_time_sync.patch
# 或
patch -p1 < <path>/gettimeofday_time_sync.patch
```

补丁内容：在 `components/libc/compilers/common/ctime.c` 的 `gettimeofday()` 中，
RTC 读取失败后回退到跨核时间同步锚点（用 `PKG_USING_TIME_SYNC` 宏保护）：

```c
#ifdef PKG_USING_TIME_SYNC
    if (tv != RT_NULL)
    {
        rt_uint64_t sec;
        rt_uint32_t usec;
        if (time_sync_get_wallclock_sec_us(&sec, &usec) == 0)
        {
            tv->tv_sec  = (time_t)sec;
            tv->tv_usec = (suseconds_t)usec;
            return 0;
        }
    }
#endif
```

然后在 `rtconfig.h` 使能 `ULOG_OUTPUT_TIME`、`ULOG_TIME_USING_TIMESTAMP` 与
`PKG_USING_TIME_SYNC`，ulog 即输出 `MM-DD HH:MM:SS.mmm` 墙钟时间戳。

> `time_sync_get_wallclock_sec_us` 已在 `time_sync` 组件（`time_sync.c`/`time_sync.h`）
> 中提供，补丁只依赖 time_sync 组件，不依赖本示例。

### 方式 B：直接改 ulog 时间戳分支

修改 `components/utilities/ulog/ulog.c` 的 `ULOG_TIME_USING_TIMESTAMP` 分支，
用 `time_sync_format_wallclock()` 替代原来的 `gettimeofday()` + `localtime_r()` 组合，
可输出 `YYYY-MM-DD HH:MM:SS.mmm`（含年份，比 ulog 原生格式更完整）。

## 时区说明

`time_sync` 换算出的 `wall_snap` 是 UTC（Unix epoch）。`time_sync_format_wallclock()`
输出的是 UTC 时间。若需本地时间（如 +08:00），在秒上加时区偏移：

```c
sec += 8 * 3600;   /* +08:00 */
```

## 编译验证

本示例自包含（不依赖 libc 的 sys/time.h），可直接编译：

```bash
aarch64-none-linux-gnu-gcc -c ulog_timestamp_demo.c -Wall -Wextra \
    -I <rtthread>/include \
    -I <rtthread>/libcpu/aarch64/common/include \
    -I <rtthread>/components/finsh \
    -I <time_sync.h 所在目录> \
    -I <rtconfig.h 所在目录>
```

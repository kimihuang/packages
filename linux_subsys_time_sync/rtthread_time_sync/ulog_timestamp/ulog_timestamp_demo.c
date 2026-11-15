/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ulog_timestamp_demo.c - ulog 墙钟时间戳接入示例
 *
 * 演示如何把 time_sync 组件换算出的墙钟时间接入 ulog，让 RT-Thread 的
 * ulog 日志时间戳从 tick 变成 "YYYY-MM-DD HH:MM:SS.mmm" 墙钟格式，
 * 与 Linux（大核）经 NTP 校准的日志时间戳对齐。
 *
 * 本示例自包含（不依赖 libc 的 sys/time.h），提供：
 *  1. time_sync_format_wallclock() —— 墙钟纳秒格式化成字符串
 *  2. time_sync_get_wallclock_sec_us() —— 拆出秒 + 微秒（供 gettimeofday 接入）
 *
 * 完整的 gettimeofday / ulog.c 接入补丁见同目录 README.md。
 */

#include <rtthread.h>
#include "time_sync.h"

/*
 * time_sync_civil_from_days - 自 epoch 的天数转公历日期
 *
 * Howard Hinnant 的 civil_from_days 算法（public domain），
 * 自包含、无溢出、无 libc 依赖。
 */
static void time_sync_civil_from_days(rt_int64_t z, int *year, int *mon, int *day)
{
    int era;
    unsigned int doe, yoe, doy, mp;
    int y;
    unsigned int d, m;

    z += 719468;
    era = (int)((z >= 0 ? z : z - 146096) / 146097);
    doe = (unsigned int)(z - (rt_int64_t)era * 146097);           /* [0, 146096] */
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  /* [0, 399] */
    y   = (int)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                /* [0, 365] */
    mp  = (5 * doy + 2) / 153;                                    /* [0, 11] */
    d   = doy - (153 * mp + 2) / 5 + 1;                           /* [1, 31] */
    m   = mp + (mp < 10 ? 3 : (unsigned int)-9);                  /* [1, 12] */

    *year = y + (int)(m <= 2);
    *mon  = (int)m;
    *day  = (int)d;
}

/*
 * time_sync_format_wallclock - 墙钟纳秒格式化成 "YYYY-MM-DD HH:MM:SS.mmm"
 *
 * @buf: 输出缓冲区
 * @len: 缓冲区长度（建议 >= 24）
 *
 * 输出 UTC 时间；如需本地时间（如 +08:00），在 sec 上加上时区偏移秒即可。
 */
void time_sync_format_wallclock(char *buf, rt_size_t len)
{
    rt_uint64_t ns  = time_sync_get_wallclock_ns();
    rt_uint64_t sec = ns / 1000000000ULL;
    rt_uint32_t usec = (rt_uint32_t)((ns % 1000000000ULL) / 1000ULL);
    rt_uint32_t ms   = usec / 1000;
    rt_uint32_t day_sec = (rt_uint32_t)(sec % 86400ULL);
    rt_uint32_t hh, mm, ss;
    int year, mon, day;

    hh = day_sec / 3600;
    mm = (day_sec % 3600) / 60;
    ss = day_sec % 60;

    time_sync_civil_from_days((rt_int64_t)(sec / 86400ULL), &year, &mon, &day);

    rt_snprintf(buf, len, "%04d-%02d-%02d %02u:%02u:%02u.%03u",
                year, mon, day, hh, mm, ss, ms);
}

/*
 * time_sync_ulog_demo - 演示函数
 *
 * 初始化 time_sync，打印当前墙钟时间戳（纳秒 + 格式化字符串）。
 */
static int time_sync_ulog_demo(void)
{
    char ts[32];
    rt_uint64_t sec;
    rt_uint32_t usec;

    if (time_sync_init() != RT_EOK)
    {
        rt_kprintf("[ulog_demo] time_sync 初始化失败，请检查共享内存锚点\n");
        return -RT_ERROR;
    }

    time_sync_format_wallclock(ts, sizeof(ts));
    rt_kprintf("[ulog_demo] wall clock = %s\n", ts);
    rt_kprintf("[ulog_demo] wall clock ns = %llu\n",
               (unsigned long long)time_sync_get_wallclock_ns());

    if (time_sync_get_wallclock_sec_us(&sec, &usec) == 0)
        rt_kprintf("[ulog_demo] sec=%llu usec=%u\n",
                   (unsigned long long)sec, (unsigned int)usec);

    return RT_EOK;
}

/* 开机自动运行（需使能 RT_USING_COMPONENTS_INIT） */
#ifdef RT_USING_COMPONENTS_INIT
INIT_APP_EXPORT(time_sync_ulog_demo);
#endif

/* 也可在 msh 命令行手动执行 */
#ifdef RT_USING_FINSH
#include <finsh.h>
/* finsh.h 的 MSH_CMD_EXPORT 内部把 int(*)(void) 强转为 long(*)(void)，
 * 会触发 -Wcast-function-type 警告（RT-Thread 头文件固有行为），此处局部抑制 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
MSH_CMD_EXPORT(time_sync_ulog_demo, time_sync ulog timestamp demo);
#pragma GCC diagnostic pop
#endif

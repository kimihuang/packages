/**
 * @file example_usage.c
 * @brief mark_point 模块使用示例
 *
 * 编译方式（gcc 本地测试）：
 *   gcc -Wall -Wextra -o mark_test example_usage.c
 *
 * 裸机使用方式（真实硬件）：
 *   将下面 #include 之前的宏定义移到对应 .c 文件的顶部，
 *   把 MARK_BUF_ADDR 改为实际的 SRAM 地址即可。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========== 配置 ========== */
/* 本地测试：使用 malloc 分配仿真缓冲区 */
#define MARK_SIM_BUFFER_SIZE 512
#define MARK_BUF_ADDR  ((uintptr_t)g_mark_buffer)
#define MARK_BUF_SIZE  (MARK_SIM_BUFFER_SIZE)
#define MARK_OVERFLOW_WRAP   /* 满后回绕（注释掉则满后停止） */
#define MARK_ENABLE
#define MARK_DUMP_PRINTF     /* 使用 printf 输出 */

/* ========== 仿真缓冲区 ========== */
static uint8_t g_mark_buffer[MARK_SIM_BUFFER_SIZE] __attribute__((aligned(8)));

#include "mark_point.h"

/* ========== 演示函数 ========== */

static void demo_basic_mark(void)
{
    printf("\n===== 基础打点演示 =====\n");

    mark_init();
    printf("初始化完成, capacity=%lu\n", (unsigned long)MARK_CAPACITY);

    MARK_POINT(0x0001);  /* 启动开始 */
    MARK_POINT(0x0002);  /* 时钟初始化完成 */
    MARK_POINT(0x0003);  /* 外设初始化完成 */

    printf("已记录 %u 条打点\n", (unsigned)mark_count());

    /* 输出打点 */
    mark_dump();

    /* 读取单条 */
    mark_entry_t e;
    if (mark_get(0, &e)) {
        printf("第 0 条: id=0x%04X data=0x%04X\n", e.id, e.data);
    }
}

static void demo_value_mark(void)
{
    printf("\n===== 带数据打点演示 =====\n");

    mark_init();

    /* 模拟 ADC 采样 */
    for (int i = 0; i < 5; i++) {
        uint16_t adc_val = (uint16_t)(i * 100 + 500);
        MARK_POINT_VAL(0x0100, adc_val);
    }

    printf("已记录 %u 条打点\n", (unsigned)mark_count());
    mark_dump();
}

static void demo_tag_mark(void)
{
    printf("\n===== 标签打点演示 =====\n");

    mark_init();

    /* 函数进入/退出 */
    MARK_POINT_TAG(MK_TAG_ENTER, 1, 0);
    MARK_POINT_TAG(MK_TAG_INFO,  2, 42);
    MARK_POINT_TAG(MK_TAG_WARN,  3, 100);
    MARK_POINT_TAG(MK_TAG_EXIT,  1, 0);

    printf("已记录 %u 条打点\n", (unsigned)mark_count());
    mark_dump();
}

static void demo_overflow_stop(void)
{
    printf("\n===== 溢出停止演示（需要注释 MARK_OVERFLOW_WRAP）=====\n");
    printf("当前为 WRAP 模式，溢出停止模式仅在未定义 MARK_OVERFLOW_WRAP 时生效\n");
    printf("请在实际使用时根据需要选择一种模式\n");

#ifdef MARK_OVERFLOW_WRAP
    printf("[WRAP mode] 满后自动回绕覆盖\n");
#else
    printf("[STOP mode] 满后停止记录\n");
    mark_init();
    for (int i = 0; i < (int)MARK_CAPACITY + 10; i++) {
        MARK_POINT(0x0100 + (i & 0xFF));
    }
    printf("尝试写入 %d 条, 实际记录 %u 条, overflow=%d\n",
           (int)MARK_CAPACITY + 10, (unsigned)mark_count(), mark_is_overflow());
#endif
}

static void demo_overflow_wrap(void)
{
    printf("\n===== 回绕覆盖演示 =====\n");

    mark_init();

    uint32_t cap = MARK_CAPACITY;
    printf("容量: %u 条, 写入: %u 条\n", (unsigned)cap, (unsigned)(cap + 5));

    for (uint32_t i = 0; i < cap + 5; i++) {
        MARK_POINT_VAL(0x0200, (uint16_t)(i + 1));
    }

    printf("实际有效: %u 条, wrapped=%d\n",
           (unsigned)mark_count(), mark_is_overflow());

    printf("\n最近 5 条（应该是 cap+1 到 cap+5）:\n");
    mark_dump_last(5);

    mark_entry_t e;
    mark_get(0, &e);
    printf("最旧条目: id=0x%04X data=0x%04X（期望 data=6，data=1~5 已被覆盖）\n",
           e.id, e.data);
}

static void demo_api_validation(void)
{
    printf("\n===== API 健壮性测试 =====\n");

    printf("mark_is_ready() before init: %d\n", mark_is_ready());
    printf("mark_count() before init: %u\n", (unsigned)mark_count());

    mark_init();
    printf("mark_is_ready() after init: %d\n", mark_is_ready());

    mark_entry_t e;
    printf("mark_get(99999) out of range: %d (expected 0)\n", mark_get(99999, &e));
    printf("mark_get(NULL ptr): %d (expected 0)\n", mark_get(0, NULL));
    printf("mark_last() on empty: %d (expected 0)\n", mark_last(&e));
}

static void demo_macro_disabled(void)
{
    printf("\n===== 零开销模式说明 =====\n");
    printf("当未定义 MARK_ENABLE 时:\n");
    printf("  MARK_POINT(id)        -> ((void)0)\n");
    printf("  MARK_POINT_VAL(id,v)  -> ((void)0)\n");
    printf("  MARK_ENTER()          -> ((void)0)\n");
    printf("  MARK_EXIT(ret)        -> ((void)0)\n");
    printf("  所有打点宏展开为空，编译后零代码、零数据开销\n");
}

int main(void)
{
    printf("mark_point 模块示例\n");
    printf("entry_size=%d bytes, capacity=%lu entries, buf_size=%d\n",
           (int)sizeof(mark_entry_t), (unsigned long)MARK_CAPACITY, MARK_BUF_SIZE);
    printf("ctrl_size=%zu bytes\n", sizeof(mark_ctrl_t));

    demo_basic_mark();
    demo_value_mark();
    demo_tag_mark();
    demo_overflow_wrap();
    demo_overflow_stop();
    demo_api_validation();
    demo_macro_disabled();

    printf("\n全部演示完成\n");
    return 0;
}

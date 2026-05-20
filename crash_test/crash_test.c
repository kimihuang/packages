/*
 * crash_test.c - 产生不同类型崩溃的测试程序，用于 core dump 分析演示
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* ====== 场景1: 空指针解引用 (SIGSEGV) ====== */
static void crash_null_deref(void)
{
    int *ptr = NULL;
    printf("[场景1] 空指针解引用: *NULL ...\n");
    int val = *ptr;  /* SIGSEGV */
    (void)val;
}

/* ====== 场景2: 数组越界写入 (SIGSEGV) ====== */
static void crash_buffer_overflow(void)
{
    char buf[8];
    printf("[场景2] 缓冲区越界写入: buf[10000] = 'X' ...\n");
    buf[10000] = 'X';  /* 栈上越界，可能 SIGSEGV */
}

/* ====== 场景3: 非法内存访问 (SIGSEGV) ====== */
static void crash_invalid_addr(void)
{
    int *ptr = (int *)0xDEADBEEF;
    printf("[场景3] 非法地址访问: *(0xDEADBEEF) ...\n");
    *ptr = 42;  /* SIGSEGV: 写入未映射地址 */
}

/* ====== 场景4: abort (SIGABRT) ====== */
static void crash_abort(void)
{
    printf("[场景4] 调用 abort() ...\n");
    abort();
}

/* ====== 场景5: 使用已释放内存 (Use-After-Free) ====== */
static void crash_use_after_free(void)
{
    int *ptr = (int *)malloc(sizeof(int));
    *ptr = 42;
    printf("[场景5] Use-After-Free: 访问已释放内存 ...\n");
    free(ptr);
    printf("  已释放 ptr=%p, 现在读取 *ptr = %d\n", ptr, *ptr);  /* UAF: 可能不立即崩溃 */
    /* 再释放一次造成 double-free → SIGABRT */
    free(ptr);
}

/* ====== 场景6: 栈溢出 (SIGSEGV) ====== */
static void crash_stack_overflow(int depth)
{
    char big[4096];
    memset(big, 'A', sizeof(big));
    big[0] = 'B';
    printf("[场景6] 栈溢出: depth=%d\n", depth);
    crash_stack_overflow(depth + 1);
}

/* ====== 场景7: 除零 (SIGFPE) ====== */
static void crash_divide_zero(void)
{
    volatile int a = 1;
    volatile int b = 0;
    printf("[场景7] 整数除零: %d / %d ...\n", a, b);
    int c = a / b;  /* SIGFPE */
    (void)c;
}

/* ====== 场景8: 自定义信号处理 + crash ====== */
static void signal_handler(int sig)
{
    printf("  [信号处理] 收到信号 %d, 即将崩溃!\n", sig);
}

static void crash_with_signal_handler(void)
{
    signal(SIGSEGV, signal_handler);
    printf("[场景8] 设置 SIGSEGV handler 后触发段错误 ...\n");
    int *ptr = NULL;
    *ptr = 99;  /* SIGSEGV → handler → 再次 SIGSEGV (默认终止) */
}

/* ====== 主函数: 选择崩溃场景 ====== */
int main(int argc, char *argv[])
{
    int scenario = 1;
    if (argc > 1) {
        scenario = atoi(argv[1]);
    }

    printf("=== crash_test: 场景 %d ===\n", scenario);

    switch (scenario) {
    case 1:  crash_null_deref();          break;
    case 2:  crash_buffer_overflow();     break;
    case 3:  crash_invalid_addr();        break;
    case 4:  crash_abort();               break;
    case 5:  crash_use_after_free();      break;
    case 6:  crash_stack_overflow(0);     break;
    case 7:  crash_divide_zero();         break;
    case 8:  crash_with_signal_handler(); break;
    default:
        printf("未知场景: %d\n", scenario);
        printf("用法: %s [1-8]\n", argv[0]);
        printf("  1: 空指针解引用 (SIGSEGV)\n");
        printf("  2: 缓冲区越界写入 (SIGSEGV)\n");
        printf("  3: 非法地址访问 (SIGSEGV)\n");
        printf("  4: abort (SIGABRT)\n");
        printf("  5: Use-After-Free + double-free (SIGABRT)\n");
        printf("  6: 栈溢出 (SIGSEGV)\n");
        printf("  7: 整数除零 (SIGFPE)\n");
        printf("  8: 自定义信号处理 + crash\n");
        return 1;
    }

    printf("正常退出 (未崩溃)\n");
    return 0;
}

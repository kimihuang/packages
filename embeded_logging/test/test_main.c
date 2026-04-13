/**
 * @file test_main.c
 * @brief 测试入口 — 汇总所有测试
 */

#include <stdio.h>
#include <stdlib.h>

/* 测试函数声明 */
extern int test_elog(void);
extern int test_elog_buf(void);
extern int test_elog_format(void);
extern int test_elog_stats(void);
extern int test_elog_prune(void);
extern int test_elog_event(void);
extern int test_elog_transport(void);
extern int test_elog_transport_file(void);
extern int test_elog_port(void);

typedef int (*test_func_t)(void);

static const struct {
    const char* name;
    test_func_t func;
} tests[] = {
    { "elog (core)",        test_elog },
    { "elog_buf",           test_elog_buf },
    { "elog_format",        test_elog_format },
    { "elog_stats",         test_elog_stats },
    { "elog_prune",         test_elog_prune },
    { "elog_event",         test_elog_event },
    { "elog_transport",     test_elog_transport },
    { "elog_transport_file", test_elog_transport_file },
    { "elog_port",            test_elog_port },
};

int main(void) {
    int passed = 0, failed = 0;

    printf("========================================\n");
    printf("  elog test suite\n");
    printf("========================================\n\n");

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        printf("[RUN ] %s\n", tests[i].name);
        fflush(stdout);
        int ret = tests[i].func();
        if (ret == 0) {
            printf("[PASS] %s\n\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s (ret=%d)\n\n", tests[i].name, ret);
            failed++;
        }
    }

    printf("========================================\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           passed, failed, (int)(sizeof(tests) / sizeof(tests[0])));
    printf("========================================\n");

    return failed > 0 ? 1 : 0;
}

/**
 * @file test_elog_port.c
 * @brief Port 层单元测试
 */

#include "elog_port.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_mutex_basic(void) {
    printf("  test_mutex_basic...\n");

    elog_mutex_t m;
    elog_mutex_init(&m);
    elog_mutex_lock(&m);
    elog_mutex_unlock(&m);
    elog_mutex_destroy(&m);

    /* NULL 不崩溃 */
    elog_mutex_lock(NULL);
    elog_mutex_unlock(NULL);
    elog_mutex_init(NULL);
    elog_mutex_destroy(NULL);
}

static void test_cond_basic(void) {
    printf("  test_cond_basic...\n");

    elog_mutex_t m;
    elog_cond_t c;
    elog_mutex_init(&m);
    elog_cond_init(&c);
    elog_mutex_lock(&m);
    elog_cond_signal(&c);
    elog_mutex_unlock(&m);
    elog_cond_destroy(&c);
    elog_mutex_destroy(&m);

    elog_cond_init(NULL);
    elog_cond_signal(NULL);
    elog_cond_destroy(NULL);
}

static void test_time(void) {
    printf("  test_time...\n");

    uint32_t now = elog_port_now();
    /* 2020-01-01 ~ 2030-01-01 范围: 1577836800 ~ 1893456000 */
    printf("    now = %u\n", now);
    assert(now >= 1577836800u);
}

static void test_localtime(void) {
    printf("  test_localtime...\n");

    /* 2024-01-01 08:00:00 UTC+8 → 本地: 2024-01-01 08:00:00 */
    uint32_t ts = 1704067200;
    int hour, minute, second, day, month;

    elog_port_localtime(ts, &hour, &minute, &second, &day, &month);

    assert(hour == 8);
    assert(minute == 0);
    assert(second == 0);
    assert(day == 1);
    assert(month == 1);

    printf("    %d-%02d-%02d %02d:%02d:%02d\n", month, day, hour, minute, second);

    /* NULL 参数不崩溃 */
    elog_port_localtime(ts, NULL, NULL, NULL, NULL, NULL);
}

static void test_pid_tid(void) {
    printf("  test_pid_tid...\n");

    uint16_t pid = elog_port_getpid();
    uint16_t tid = elog_port_gettid();

    printf("    pid=%u tid=%u\n", pid, tid);
    /* Linux 下应该非零 */
    assert(pid > 0);
    assert(tid > 0);
}

static void test_atomic_inc(void) {
    printf("  test_atomic_inc...\n");

    volatile uint32_t val = 0;

    uint32_t old = elog_port_atomic_inc(&val);
    assert(old == 0);
    assert(val == 1);

    old = elog_port_atomic_inc(&val);
    assert(old == 1);
    assert(val == 2);

    for (int i = 0; i < 100; i++) {
        elog_port_atomic_inc(&val);
    }
    assert(val == 102);
}

static void test_isr_save_restore(void) {
    printf("  test_isr_save_restore...\n");

    /* Linux 下是空操作，验证不崩溃 */
    elog_isr_state_t state = elog_port_isr_save();
    elog_port_isr_restore(state);
}

int test_elog_port(void) {
    printf("test_elog_port:\n");

    test_mutex_basic();
    test_cond_basic();
    test_time();
    test_localtime();
    test_pid_tid();
    test_atomic_inc();
    test_isr_save_restore();

    return 0;
}

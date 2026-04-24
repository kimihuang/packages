/*
 * test_timer.c - group_timer: Timer/Counter IP tests
 *
 * Mock IP: register-based timer simulated in software.
 * Each TEST_CASE receives a `timer_test_param_t *` via the `data` parameter,
 * set up through constructor(104) calling cv_case_set_data().
 */
#include "cv_macros.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ---- Mock Timer controller registers ---- */

#define TIMER_CTRL_EN       (1 << 0)   /* enable          */
#define TIMER_CTRL_PERIODIC (1 << 1)   /* auto-reload     */
#define TIMER_CTRL_IE       (1 << 2)   /* interrupt en    */

#define TIMER_STAT_EXPIRED  (1 << 0)   /* count reached 0 */

typedef struct {
    uint32_t ctrl;
    uint32_t load_val;
    uint32_t count;
    uint32_t status;
    uint32_t interrupt_count;
} timer_reg_t;

static timer_reg_t g_timer;

static void timer_reset(timer_reg_t *t)
{
    memset(t, 0, sizeof(*t));
}

/* Load value into timer (does not start) */
static void timer_load(timer_reg_t *t, uint32_t val)
{
    t->load_val = val;
    t->count = val;
    t->status = 0;
    t->interrupt_count = 0;
}

/* Start the timer */
static void timer_start(timer_reg_t *t, uint32_t mode)
{
    t->ctrl |= TIMER_CTRL_EN | mode;
}

/* Stop the timer */
static void timer_stop(timer_reg_t *t)
{
    t->ctrl &= ~TIMER_CTRL_EN;
}

/* Tick the timer once */
static void timer_tick(timer_reg_t *t)
{
    if (!(t->ctrl & TIMER_CTRL_EN)) return;

    if (t->count > 0) {
        t->count--;
    }

    if (t->count == 0) {
        t->status |= TIMER_STAT_EXPIRED;
        t->interrupt_count++;
        if (t->ctrl & TIMER_CTRL_PERIODIC) {
            t->count = t->load_val;   /* auto-reload */
            t->status &= ~TIMER_STAT_EXPIRED;
        } else {
            timer_stop(t);            /* single-shot stops */
        }
    }
}

/* Tick N times */
static void timer_tick_n(timer_reg_t *t, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        timer_tick(t);
    }
}

/* ---- Test parameter struct (passed via void *data) ---- */

typedef struct {
    uint32_t load_val;
    uint32_t tick_count;
    int      expect_expired;    /* 1: status should have EXPIRED flag */
    uint32_t expect_count;      /* expected final count value */
    int      expect_interrupts; /* expected number of interrupts */
} timer_test_param_t;

/* ---- Group / Module registration ---- */

TEST_GROUP(group_timer);

static void timer_group_pre(void *data)
{
    (void)data;
    printf("  [GROUP PRE_TEST] group_timer: init timer IP...\n");
    timer_reset(&g_timer);
}

static void timer_group_post(void *data)
{
    (void)data;
    printf("  [GROUP POST_TEST] group_timer: reset timer IP...\n");
    timer_reset(&g_timer);
}

GROUP_PRE_TEST(group_timer, timer_group_pre);
GROUP_POST_TEST(group_timer, timer_group_post);

/* ==================== module_timer_basic ==================== */

TEST_MODULE(group_timer, module_timer_basic);

static void timer_basic_pre(void *data)
{
    (void)data;
    printf("    [MODULE PRE_TEST] reset timer registers\n");
    timer_reset(&g_timer);
}

static void timer_basic_post(void *data)
{
    (void)data;
    printf("    [MODULE POST_TEST] stop & cleanup timer\n");
    timer_stop(&g_timer);
}

MODULE_PRE_TEST(module_timer_basic, timer_basic_pre);
MODULE_POST_TEST(module_timer_basic, timer_basic_post);

TEST_CASE(module_timer_basic, test_timer_single_shot)
{
    timer_test_param_t *p = (timer_test_param_t *)data;

    timer_load(&g_timer, p->load_val);
    timer_start(&g_timer, 0);            /* single-shot */
    timer_tick_n(&g_timer, p->tick_count);

    CV_ASSERT_EQ((long)g_timer.interrupt_count, (long)p->expect_interrupts);
    if (p->expect_expired) {
        CV_ASSERT(g_timer.status & TIMER_STAT_EXPIRED);
    } else {
        CV_ASSERT(!(g_timer.status & TIMER_STAT_EXPIRED));
    }
    CV_ASSERT_EQ((long)g_timer.count, (long)p->expect_count);
}

TEST_CASE(module_timer_basic, test_timer_stop)
{
    timer_test_param_t *p = (timer_test_param_t *)data;

    timer_load(&g_timer, p->load_val);
    timer_start(&g_timer, 0);
    timer_tick_n(&g_timer, p->tick_count);
    timer_stop(&g_timer);

    /* After stop, additional ticks should not change count */
    uint32_t frozen = g_timer.count;
    timer_tick_n(&g_timer, 10);
    CV_ASSERT_EQ((long)g_timer.count, (long)frozen);
    CV_ASSERT(!(g_timer.ctrl & TIMER_CTRL_EN));
}

/* ==================== module_timer_interrupt ==================== */

TEST_MODULE(group_timer, module_timer_interrupt);

static void timer_intr_pre(void *data)
{
    (void)data;
    printf("    [MODULE PRE_TEST] init interrupt controller\n");
    timer_reset(&g_timer);
}

static void timer_intr_post(void *data)
{
    (void)data;
    printf("    [MODULE POST_TEST] cleanup interrupt controller\n");
    timer_stop(&g_timer);
}

MODULE_PRE_TEST(module_timer_interrupt, timer_intr_pre);
MODULE_POST_TEST(module_timer_interrupt, timer_intr_post);

TEST_CASE(module_timer_interrupt, test_timer_periodic)
{
    timer_test_param_t *p = (timer_test_param_t *)data;

    timer_load(&g_timer, p->load_val);
    timer_start(&g_timer, TIMER_CTRL_PERIODIC);
    timer_tick_n(&g_timer, p->tick_count);

    CV_ASSERT_EQ((long)g_timer.interrupt_count, (long)p->expect_interrupts);
    /* Periodic mode: timer should still be running */
    CV_ASSERT(g_timer.ctrl & TIMER_CTRL_EN);
    CV_ASSERT_EQ((long)g_timer.count, (long)p->expect_count);
}

TEST_CASE(module_timer_interrupt, test_timer_periodic_overflow)
{
    timer_test_param_t *p = (timer_test_param_t *)data;

    timer_load(&g_timer, p->load_val);
    timer_start(&g_timer, TIMER_CTRL_PERIODIC);
    timer_tick_n(&g_timer, p->tick_count);

    /* Verify timer wrapped around correct number of times */
    uint32_t expected_cycles = p->tick_count / p->load_val;
    CV_ASSERT_EQ((long)g_timer.interrupt_count, (long)expected_cycles);
    CV_ASSERT(g_timer.ctrl & TIMER_CTRL_EN);
}

/* ---- Parameter data and cv_case_set_data (constructor 104) ---- */

/* single-shot: load=5, tick=5 => expired, count=0 */
static timer_test_param_t param_single_shot = {
    .load_val = 5, .tick_count = 5,
    .expect_expired = 1, .expect_count = 0, .expect_interrupts = 1,
};

/* stop: load=100, tick=30, stop => count=70 */
static timer_test_param_t param_stop = {
    .load_val = 100, .tick_count = 30,
    .expect_expired = 0, .expect_count = 70, .expect_interrupts = 0,
};

/* periodic: load=4, tick=10 => 2 wraps, count=2 (10 % 4) */
static timer_test_param_t param_periodic = {
    .load_val = 4, .tick_count = 10,
    .expect_expired = 0, .expect_count = 2, .expect_interrupts = 2,
};

/* periodic overflow: load=3, tick=9 => 3 wraps, count=0 (9 % 3) */
static timer_test_param_t param_periodic_overflow = {
    .load_val = 3, .tick_count = 9,
    .expect_expired = 0, .expect_count = 0, .expect_interrupts = 3,
};

__attribute__((constructor(104)))
static void __cv_setdata_timer_single(void) {
    cv_case_set_data(test_timer_single_shot_cvcase, &param_single_shot);
}

__attribute__((constructor(104)))
static void __cv_setdata_timer_stop(void) {
    cv_case_set_data(test_timer_stop_cvcase, &param_stop);
}

__attribute__((constructor(104)))
static void __cv_setdata_timer_periodic(void) {
    cv_case_set_data(test_timer_periodic_cvcase, &param_periodic);
}

__attribute__((constructor(104)))
static void __cv_setdata_timer_overflow(void) {
    cv_case_set_data(test_timer_periodic_overflow_cvcase, &param_periodic_overflow);
}

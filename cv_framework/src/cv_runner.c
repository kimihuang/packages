/*
 * cv_runner.c - Test runner: traverse linked lists, call hooks, collect stats
 */
#include "cv_test.h"
#include "cv_option.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Shuffle helper (Fisher-Yates via list) ---- */
static void shuffle_list(struct list_head *head)
{
    int count = 0;
    struct list_head *nodes[1024];

    struct list_head *pos;
    list_for_each(pos, head) {
        if (count < 1024) nodes[count] = pos;
        count++;
    }

    if (count < 2) return;

    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        if (i != j) {
            struct list_head tmp;
            memcpy(&tmp, nodes[i], sizeof(tmp));
            memcpy(nodes[i], nodes[j], sizeof(tmp));
            memcpy(nodes[j], &tmp, sizeof(tmp));

            nodes[i]->next->prev = nodes[i];
            nodes[i]->prev->next = nodes[i];
            nodes[j]->next->prev = nodes[j];
            nodes[j]->prev->next = nodes[j];
        }
    }
}

/* ---- Should we print framework-level output? ---- */
static int should_print(const cv_test_opts_t *opts)
{
    return !(opts && opts->silent);
}

/* ---- Internal: run a single case ---- */
static int run_single_case(cv_test_case_t *c, const cv_test_opts_t *opts)
{
    if (!c->enabled) {
        c->result = CV_RESULT_SKIP;
        return 0;
    }

    c->result = CV_RESULT_UNINIT;
    c->errmsg[0] = '\0';
    cv_test_set_current_case(c);

    /* pre_test hook */
    if (c->pre_test) {
        if (opts && opts->verbose) printf("      [CASE PRE_TEST] %s\n", c->name);
        c->pre_test(NULL);
    }

    /* Execute test function */
    c->func(c->data);

    /* post_test hook */
    if (c->post_test) {
        if (opts && opts->verbose) printf("      [CASE POST_TEST] %s\n", c->name);
        c->post_test(NULL);
    }

    /* Determine result */
    if (c->result == CV_RESULT_UNINIT) {
        c->result = CV_RESULT_PASS;
    }

    cv_test_set_current_case(NULL);

    /* Print result */
    if (should_print(opts)) {
        switch (c->result) {
        case CV_RESULT_PASS:  printf("      [PASS] %s\n", c->name);          break;
        case CV_RESULT_FAIL:  printf("      [FAIL] %s  <-- %s\n", c->name, c->errmsg); break;
        case CV_RESULT_ERROR: printf("      [ERROR] %s  <-- %s\n", c->name, c->errmsg); break;
        case CV_RESULT_SKIP:  printf("      [SKIP] %s\n", c->name);          break;
        default: break;
        }
    }

    /* fail-fast check */
    if (opts && opts->fail_fast && c->result == CV_RESULT_FAIL) {
        return 1;
    }
    return 0;
}

/* ---- Internal: run a single module ---- */
static int run_single_module(cv_test_module_t *m, const cv_test_opts_t *opts)
{
    if (!m->enabled) {
        struct list_head *c_pos;
        list_for_each(c_pos, &m->case_list) {
            m->skip_count++;
        }
        return 0;
    }

    if (should_print(opts)) printf("  [MODULE] %s\n", m->name);

    if (m->pre_test) {
        if (opts && opts->verbose) printf("    [MODULE PRE_TEST] %s\n", m->name);
        m->pre_test(NULL);
    }

    if (opts && opts->shuffle) {
        shuffle_list(&m->case_list);
    }

    struct list_head *c_pos;
    list_for_each(c_pos, &m->case_list) {
        cv_test_case_t *c = list_entry(c_pos, cv_test_case_t, case_node);
        int abort_flag = run_single_case(c, opts);

        switch (c->result) {
        case CV_RESULT_PASS:  m->pass_count++;  break;
        case CV_RESULT_FAIL:  m->fail_count++;  break;
        case CV_RESULT_ERROR: m->error_count++; break;
        case CV_RESULT_SKIP:  m->skip_count++;  break;
        default: break;
        }

        if (abort_flag) return 1;
    }

    if (m->post_test) {
        if (opts && opts->verbose) printf("    [MODULE POST_TEST] %s\n", m->name);
        m->post_test(NULL);
    }

    return 0;
}

/* ---- Internal: run a single group ----
 * Accumulates module stats into group AND framework after each module,
 * so stats are correct even if fail-fast aborts mid-group.
 */
static int run_single_group(cv_test_group_t *g, const cv_test_opts_t *opts)
{
    cv_test_framework_t *fw = cv_framework_get();

    if (!g->enabled) {
        struct list_head *m_pos;
        list_for_each(m_pos, &g->module_list) {
            cv_test_module_t *m = list_entry(m_pos, cv_test_module_t, module_node);
            struct list_head *c_pos;
            list_for_each(c_pos, &m->case_list) {
                m->skip_count++;
            }
            g->skip_count  += m->skip_count;
            fw->total_skip += m->skip_count;
        }
        return 0;
    }

    if (should_print(opts)) printf("[GROUP] %s\n", g->name);

    if (g->pre_test) {
        if (opts && opts->verbose) printf("  [GROUP PRE_TEST] %s\n", g->name);
        g->pre_test(NULL);
    }

    struct list_head *m_pos;
    list_for_each(m_pos, &g->module_list) {
        cv_test_module_t *m = list_entry(m_pos, cv_test_module_t, module_node);
        int abort_flag = run_single_module(m, opts);

        /* Immediately propagate module stats into group and framework */
        g->pass_count  += m->pass_count;
        g->fail_count  += m->fail_count;
        g->skip_count  += m->skip_count;
        g->error_count += m->error_count;
        fw->total_pass  += m->pass_count;
        fw->total_fail  += m->fail_count;
        fw->total_skip  += m->skip_count;
        fw->total_error += m->error_count;
        fw->total_run   += m->pass_count + m->fail_count + m->error_count;

        if (abort_flag) return 1;
    }

    if (g->post_test) {
        if (opts && opts->verbose) printf("  [GROUP POST_TEST] %s\n", g->name);
        g->post_test(NULL);
    }

    return 0;
}

/* ---- Public: run a single round ---- */
static int cv_run_round(const cv_test_opts_t *opts)
{
    cv_test_framework_t *fw = cv_framework_get();

    if (opts && opts->shuffle) {
        srand((unsigned int)(opts->repeat_count + 1));
        shuffle_list(&fw->group_list);
    }

    struct list_head *g_pos;
    list_for_each(g_pos, &fw->group_list) {
        cv_test_group_t *g = list_entry(g_pos, cv_test_group_t, group_node);
        int abort_flag = run_single_group(g, opts);
        if (abort_flag) {
            if (should_print(opts)) printf("\n  ABORTED: fail-fast on first failure\n");
            break;
        }
    }

    return fw->total_fail == 0 ? 0 : 1;
}

/* ---- Public: run with options (handles repeat logic) ---- */
int cv_run(const cv_test_opts_t *opts)
{
    int max_rounds = 1;
    if (opts) {
        if (opts->repeat_count > 1) {
            max_rounds = opts->repeat_count;
        } else if (opts->repeat_count == 0) {
            max_rounds = 0;
        }
    }

    int round = 0;
    int rounds_passed = 0;
    int rounds_failed = 0;
    int worst_pass = 999999;
    int worst_fail = 0;
    int last_ret = 0;

    while (max_rounds == 0 || round < max_rounds) {
        round++;

        if (should_print(opts)) {
            if (max_rounds > 1 || max_rounds == 0) {
                printf("===========================================\n");
                printf("  CV Test Framework v1.0  [Round %d", round);
                if (max_rounds > 0) printf("/%d", max_rounds);
                printf("]\n");
                printf("===========================================\n");
            } else {
                printf("===========================================\n");
                printf("  CV Test Framework v1.0\n");
                printf("===========================================\n");
            }
        }

        cv_test_reset_stats();
        last_ret = cv_run_round(opts);

        cv_summary();

        if (last_ret == 0) {
            rounds_passed++;
        } else {
            rounds_failed++;
        }

        cv_test_framework_t *fw = cv_framework_get();
        if (fw->total_pass < worst_pass) worst_pass = fw->total_pass;
        if (fw->total_fail > worst_fail) worst_fail = fw->total_fail;

        if (max_rounds == 0 && last_ret != 0) {
            break;
        }
    }

    /* Print round summary if multiple rounds */
    if (round > 1) {
        printf("\n===========================================\n");
        printf("  ROUND SUMMARY\n");
        printf("===========================================\n");
        printf("  Rounds: %d | All-Pass: %d | Has-Fail: %d\n",
               round, rounds_passed, rounds_failed);
        if (rounds_failed > 0) {
            printf("  Worst Round: PASS: %d  FAIL: %d\n", worst_pass, worst_fail);
        }
        printf("===========================================\n");
    }

    return last_ret;
}

/* ---- Public: run all with default options ---- */
int cv_run_all(void)
{
    return cv_run(NULL);
}

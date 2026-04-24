/*
 * cv_test.c - CV Test Framework core implementation
 */
#include "cv_test.h"
#include "cv_option.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Framework singleton ---- */
static cv_test_framework_t g_framework = {
    .group_list = { &g_framework.group_list, &g_framework.group_list },
};

cv_test_framework_t *cv_framework_get(void)
{
    return &g_framework;
}

/* ---- Current running case (for assertion reporting) ---- */
static cv_test_case_t *g_current_case = NULL;

cv_test_case_t *cv_test_get_current_case(void)
{
    return g_current_case;
}

void cv_test_set_current_case(cv_test_case_t *c)
{
    g_current_case = c;
}

void cv_test_current_case_fail(const char *file, int line, const char *msg)
{
    if (g_current_case) {
        snprintf(g_current_case->errmsg, CV_ERR_MSG_MAX,
                 "%s:%d: %s", file, line, msg);
        g_current_case->result = CV_RESULT_FAIL;
    }
}

/* ---- Allocation helpers ---- */
static cv_test_group_t *alloc_group(const char *name)
{
    cv_test_group_t *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    strncpy(g->name, name, CV_NAME_MAX - 1);
    INIT_LIST_HEAD(&g->module_list);
    INIT_LIST_HEAD(&g->group_node);
    g->enabled = 1;
    return g;
}

static cv_test_module_t *alloc_module(const char *name)
{
    cv_test_module_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    strncpy(m->name, name, CV_NAME_MAX - 1);
    INIT_LIST_HEAD(&m->case_list);
    INIT_LIST_HEAD(&m->module_node);
    m->enabled = 1;
    return m;
}

static cv_test_case_t *alloc_case(const char *name, cv_test_func_t func)
{
    cv_test_case_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    strncpy(c->name, name, CV_NAME_MAX - 1);
    INIT_LIST_HEAD(&c->case_node);
    c->func = func;
    c->result = CV_RESULT_UNINIT;
    c->enabled = 1;
    return c;
}

/* ---- Registration ---- */
cv_test_group_t *cv_group_register(const char *name)
{
    cv_test_framework_t *fw = cv_framework_get();
    cv_test_group_t *g = alloc_group(name);
    if (g) {
        list_add_tail(&g->group_node, &fw->group_list);
        fw->group_count++;
    }
    return g;
}

cv_test_module_t *cv_module_register(cv_test_group_t *group, const char *name)
{
    if (!group) return NULL;
    cv_test_module_t *m = alloc_module(name);
    if (m) {
        list_add_tail(&m->module_node, &group->module_list);
        group->module_count++;
    }
    return m;
}

cv_test_case_t *cv_case_register(cv_test_module_t *module,
                                  const char *name, cv_test_func_t func)
{
    if (!module) return NULL;
    cv_test_case_t *c = alloc_case(name, func);
    if (c) {
        list_add_tail(&c->case_node, &module->case_list);
        module->case_count++;
    }
    return c;
}

/* ---- Hooks ---- */
void cv_group_set_hooks(cv_test_group_t *g,
                        cv_test_func_t pre_test, cv_test_func_t post_test)
{
    if (!g) return;
    if (pre_test)  g->pre_test  = pre_test;
    if (post_test) g->post_test = post_test;
}

void cv_module_set_hooks(cv_test_module_t *m,
                         cv_test_func_t pre_test, cv_test_func_t post_test)
{
    if (!m) return;
    if (pre_test)  m->pre_test  = pre_test;
    if (post_test) m->post_test = post_test;
}

void cv_case_set_hooks(cv_test_case_t *c,
                       cv_test_func_t pre_test, cv_test_func_t post_test)
{
    if (!c) return;
    if (pre_test)  c->pre_test  = pre_test;
    if (post_test) c->post_test = post_test;
}

/* ---- Case parameter ---- */
void cv_case_set_data(cv_test_case_t *c, void *data)
{
    if (c) c->data = data;
}

/* ---- Enable / Disable ---- */
void cv_group_enable(cv_test_group_t *g, int enable)
{
    if (g) g->enabled = enable;
}

void cv_module_enable(cv_test_module_t *m, int enable)
{
    if (m) m->enabled = enable;
}

void cv_case_enable(cv_test_case_t *c, int enable)
{
    if (c) c->enabled = enable;
}

/* ---- Reset statistics ---- */
void cv_test_reset_stats(void)
{
    cv_test_framework_t *fw = cv_framework_get();
    fw->total_pass = 0;
    fw->total_fail = 0;
    fw->total_skip = 0;
    fw->total_error = 0;
    fw->total_run = 0;

    struct list_head *g_pos;
    list_for_each(g_pos, &fw->group_list) {
        cv_test_group_t *g = list_entry(g_pos, cv_test_group_t, group_node);
        g->pass_count = 0;
        g->fail_count = 0;
        g->skip_count = 0;
        g->error_count = 0;

        struct list_head *m_pos;
        list_for_each(m_pos, &g->module_list) {
            cv_test_module_t *m = list_entry(m_pos, cv_test_module_t, module_node);
            m->pass_count = 0;
            m->fail_count = 0;
            m->skip_count = 0;
            m->error_count = 0;

            struct list_head *c_pos;
            list_for_each(c_pos, &m->case_list) {
                cv_test_case_t *c = list_entry(c_pos, cv_test_case_t, case_node);
                c->result = CV_RESULT_UNINIT;
                c->errmsg[0] = '\0';
            }
        }
    }
}

/* ---- List tests ---- */
void cv_list_tests(int detail)
{
    cv_test_framework_t *fw = cv_framework_get();
    int total_modules = 0, total_cases = 0, disabled = 0;

    struct list_head *g_pos;
    list_for_each(g_pos, &fw->group_list) {
        cv_test_group_t *g = list_entry(g_pos, cv_test_group_t, group_node);
        if (detail) {
            printf("  [%s] %s (%d modules)\n",
                   g->enabled ? "ENABLED " : "DISABLED", g->name, g->module_count);
        } else {
            printf("  %s\n", g->name);
        }

        struct list_head *m_pos;
        list_for_each(m_pos, &g->module_list) {
            cv_test_module_t *m = list_entry(m_pos, cv_test_module_t, module_node);
            total_modules++;
            if (detail) {
                printf("    [%s] %-24s (%d cases, %d disabled)\n",
                       m->enabled ? "ENABLED " : "DISABLED",
                       m->name, m->case_count, m->case_count - m->enabled ? 0 : 0);
            } else {
                printf("    %-24s (%d cases)\n", m->name, m->case_count);
            }

            struct list_head *c_pos;
            list_for_each(c_pos, &m->case_list) {
                cv_test_case_t *c = list_entry(c_pos, cv_test_case_t, case_node);
                total_cases++;
                if (!c->enabled) disabled++;
                if (detail) {
                    printf("      [%s] %s\n",
                           c->enabled ? "ENABLED " : "DISABLED", c->name);
                } else {
                    printf("      %s\n", c->name);
                }
            }
        }
    }
    printf("\n  Total: %d groups, %d modules, %d cases", fw->group_count, total_modules, total_cases);
    if (disabled > 0) printf(" (%d disabled)", disabled);
    printf("\n");
}

/* ---- Filter application ---- */
static int match_list(const char *name, char *const *list, int count)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(name, list[i]) == 0)
            return 1;
    }
    return 0;
}

static int wildcard_match(const char *pattern, const char *str)
{
    if (!pattern || pattern[0] == '\0') return 1;
    if (!str) return 0;

    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1;
            while (*str) {
                if (wildcard_match(pattern, str)) return 1;
                str++;
            }
            return 0;
        }
        if (*pattern != *str) return 0;
        pattern++;
        str++;
    }
    while (*pattern == '*') pattern++;
    return (*pattern == '\0' && *str == '\0');
}

void cv_apply_filters(const cv_test_opts_t *opts)
{
    if (!opts) return;
    int has_group  = opts->group_count > 0;
    int has_module = opts->module_count > 0;
    int has_case   = opts->case_count > 0;
    int has_filter = opts->filter != NULL;

    if (!has_group && !has_module && !has_case && !has_filter)
        return;

    cv_test_framework_t *fw = cv_framework_get();
    struct list_head *g_pos;

    list_for_each(g_pos, &fw->group_list) {
        cv_test_group_t *g = list_entry(g_pos, cv_test_group_t, group_node);

        if (has_group && !match_list(g->name, opts->groups, opts->group_count)) {
            g->enabled = 0;
            continue;
        }

        struct list_head *m_pos;
        list_for_each(m_pos, &g->module_list) {
            cv_test_module_t *m = list_entry(m_pos, cv_test_module_t, module_node);

            if (has_module && !match_list(m->name, opts->modules, opts->module_count)) {
                m->enabled = 0;
                continue;
            }

            struct list_head *c_pos;
            list_for_each(c_pos, &m->case_list) {
                cv_test_case_t *c = list_entry(c_pos, cv_test_case_t, case_node);

                if (has_case && !match_list(c->name, opts->cases, opts->case_count)) {
                    c->enabled = 0;
                }
                if (has_filter && !wildcard_match(opts->filter, c->name)) {
                    c->enabled = 0;
                }
            }
        }
    }
}

/* ---- Summary ---- */
void cv_summary(void)
{
    cv_test_framework_t *fw = cv_framework_get();
    printf("\n===========================================\n");
    printf("  SUMMARY\n");
    printf("===========================================\n");

    int total_cases = fw->total_pass + fw->total_fail + fw->total_skip + fw->total_error;
    printf("  PASS: %d  |  FAIL: %d  |  ERROR: %d  |  SKIP: %d",
           fw->total_pass, fw->total_fail, fw->total_error, fw->total_skip);

    if (total_cases > 0 && total_cases != fw->total_run) {
        printf("  (total %d, ran %d)", total_cases, fw->total_run);
    }
    printf("\n");
}

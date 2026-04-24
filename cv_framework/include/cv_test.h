/*
 * cv_test.h - CV Test Framework core data structures and API
 */
#ifndef CV_TEST_H
#define CV_TEST_H

#include "cv_list.h"
#include "cv_option.h"
#include <stddef.h>

/* ---- Constants ---- */
#define CV_NAME_MAX    64
#define CV_ERR_MSG_MAX 256

/* ---- Types ----
 *
 * cv_test_func_t: hook 函数类型（group/module/case 钩子），接收 void *data 参数。
 *   - group/module 钩子调用时 data 为 NULL
 *   - case 钩子调用时 data 为 cv_test_case_t.data（用户设定的参数）
 */
typedef void (*cv_test_func_t)(void *data);

typedef enum {
    CV_RESULT_UNINIT = -1,
    CV_RESULT_PASS   = 0,
    CV_RESULT_FAIL   = 1,
    CV_RESULT_SKIP   = 2,
    CV_RESULT_ERROR  = 3,
} cv_test_result_t;

/* ---- Forward declarations ---- */
typedef struct cv_test_case   cv_test_case_t;
typedef struct cv_test_module cv_test_module_t;
typedef struct cv_test_group  cv_test_group_t;

/* ---- cv_test_case: minimum execution unit ----
 *
 * func 接收 void *data 参数，运行时由框架传入 case->data。
 * data 为用户自定义参数指针，通过 cv_case_set_data() 设置。
 * pre_test / post_test 同样接收 data，可在用例前后基于参数做准备工作。
 */
struct cv_test_case {
    char                name[CV_NAME_MAX];
    cv_test_func_t      func;
    cv_test_func_t      pre_test;
    cv_test_func_t      post_test;
    struct list_head    case_node;
    cv_test_result_t    result;
    char                errmsg[CV_ERR_MSG_MAX];
    int                 enabled;
    void               *data;     /* user-provided parameter */
};

/* ---- cv_test_module: contains multiple test_cases ---- */
struct cv_test_module {
    char                name[CV_NAME_MAX];
    struct list_head    case_list;
    struct list_head    module_node;
    int                 case_count;
    int                 pass_count;
    int                 fail_count;
    int                 skip_count;
    int                 error_count;
    cv_test_func_t      pre_test;
    cv_test_func_t      post_test;
    int                 enabled;
};

/* ---- cv_test_group: contains multiple test_modules ---- */
struct cv_test_group {
    char                name[CV_NAME_MAX];
    struct list_head    module_list;
    struct list_head    group_node;
    int                 module_count;
    int                 pass_count;
    int                 fail_count;
    int                 skip_count;
    int                 error_count;
    cv_test_func_t      pre_test;
    cv_test_func_t      post_test;
    int                 enabled;
};

/* ---- Round statistics for repeat execution ---- */
typedef struct cv_round_stat {
    int                 round;
    int                 rounds_passed;
    int                 rounds_failed;
    int                 worst_pass;
    int                 worst_fail;
} cv_round_stat_t;

/* ---- cv_test_framework: global singleton ---- */
typedef struct cv_test_framework {
    struct list_head    group_list;
    int                 group_count;
    int                 total_pass;
    int                 total_fail;
    int                 total_skip;
    int                 total_error;
    int                 total_run;
} cv_test_framework_t;

/* ---- Framework singleton ---- */
cv_test_framework_t *cv_framework_get(void);

/* ---- Registration ---- */
cv_test_group_t  *cv_group_register(const char *name);
cv_test_module_t *cv_module_register(cv_test_group_t *group, const char *name);
cv_test_case_t   *cv_case_register(cv_test_module_t *module,
                                    const char *name, cv_test_func_t func);

/* ---- Hooks ---- */
void cv_group_set_hooks(cv_test_group_t  *g,
                        cv_test_func_t pre_test, cv_test_func_t post_test);
void cv_module_set_hooks(cv_test_module_t *m,
                         cv_test_func_t pre_test, cv_test_func_t post_test);
void cv_case_set_hooks(cv_test_case_t   *c,
                       cv_test_func_t pre_test, cv_test_func_t post_test);

/* ---- Enable / Disable ---- */
void cv_group_enable(cv_test_group_t  *g,  int enable);
void cv_module_enable(cv_test_module_t *m, int enable);
void cv_case_enable(cv_test_case_t   *c,  int enable);

/* ---- Case parameter ---- */
void cv_case_set_data(cv_test_case_t *c, void *data);

/* ---- Execution ---- */
int  cv_run(const cv_test_opts_t *opts);
int  cv_run_all(void);
void cv_summary(void);

/* ---- List / Query ---- */
void cv_list_tests(int detail);
void cv_apply_filters(const cv_test_opts_t *opts);
void cv_test_reset_stats(void);

/* ---- Internal: current case tracking (for assertions) ---- */
cv_test_case_t *cv_test_get_current_case(void);
void            cv_test_set_current_case(cv_test_case_t *c);
void            cv_test_current_case_fail(const char *file, int line, const char *msg);

#endif /* CV_TEST_H */

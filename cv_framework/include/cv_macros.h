/*
 * cv_macros.h - User convenience macros for test registration and assertion
 */
#ifndef CV_MACROS_H
#define CV_MACROS_H

#include "cv_test.h"

/* ---- Registration macros (constructor auto-register) ----
 *
 * Static variables use the user-provided name directly (e.g. group_math,
 * module_add) so they can be referenced by hook macros in the same file.
 *
 * Constructor priorities: group(101) < module(102) < case(103) < hooks(104)
 */

#define TEST_GROUP(name)                                               \
    static cv_test_group_t *name;                                     \
    __attribute__((constructor(101)))                                   \
    static void __cv_reg_grp_##name(void) {                            \
        name = cv_group_register(#name);                               \
    }

#define TEST_MODULE(group, name)                                       \
    static cv_test_module_t *name;                                    \
    __attribute__((constructor(102)))                                   \
    static void __cv_reg_mod_##name(void) {                            \
        name = cv_module_register(group, #name);                       \
    }

#define TEST_CASE(module, name)                                        \
    static void name(void *data);                                      \
    static cv_test_case_t *name##_cvcase;                              \
    __attribute__((constructor(103)))                                   \
    static void __cv_reg_case_##name(void) {                           \
        name##_cvcase = cv_case_register(module, #name, name);        \
    }                                                                  \
    static void name(void *data)

/* ---- Hook macros (also use constructor, priority 104) ---- */

#define GROUP_PRE_TEST(grp, fn)                                        \
    __attribute__((constructor(104)))                                   \
    static void __cv_gpre_##grp##_##fn(void) {                        \
        cv_group_set_hooks(grp, fn, NULL);                             \
    }

#define GROUP_POST_TEST(grp, fn)                                       \
    __attribute__((constructor(104)))                                   \
    static void __cv_gpost_##grp##_##fn(void) {                       \
        cv_group_set_hooks(grp, NULL, fn);                             \
    }

#define MODULE_PRE_TEST(mod, fn)                                       \
    __attribute__((constructor(104)))                                   \
    static void __cv_mpre_##mod##_##fn(void) {                        \
        cv_module_set_hooks(mod, fn, NULL);                            \
    }

#define MODULE_POST_TEST(mod, fn)                                      \
    __attribute__((constructor(104)))                                   \
    static void __cv_mpost_##mod##_##fn(void) {                       \
        cv_module_set_hooks(mod, NULL, fn);                            \
    }

#define CASE_PRE_TEST(c, fn)                                           \
    __attribute__((constructor(104)))                                   \
    static void __cv_cpre_##c##_##fn(void) {                          \
        cv_case_set_hooks(c, fn, NULL);                                \
    }

#define CASE_POST_TEST(c, fn)                                          \
    __attribute__((constructor(104)))                                   \
    static void __cv_cpost_##c##_##fn(void) {                         \
        cv_case_set_hooks(c, NULL, fn);                                \
    }

/* ---- Assertion macros ---- */

#define CV_ASSERT(cond)                                               \
    do {                                                              \
        if (!(cond)) {                                                \
            cv_test_current_case_fail(__FILE__, __LINE__, #cond);     \
        }                                                             \
    } while (0)

#define CV_ASSERT_EQ(a, b)                                            \
    do {                                                              \
        long _ea = (long)(a), _eb = (long)(b);                        \
        if (_ea != _eb) {                                             \
            char _ebuf[CV_ERR_MSG_MAX];                               \
            snprintf(_ebuf, sizeof(_ebuf),                             \
                     "%s != %s (%ld vs %ld)", #a, #b, _ea, _eb);     \
            cv_test_current_case_fail(__FILE__, __LINE__, _ebuf);     \
        }                                                             \
    } while (0)

#define CV_ASSERT_NE(a, b)                                            \
    do {                                                              \
        long _ea = (long)(a), _eb = (long)(b);                        \
        if (_ea == _eb) {                                             \
            char _ebuf[CV_ERR_MSG_MAX];                               \
            snprintf(_ebuf, sizeof(_ebuf),                             \
                     "%s == %s (%ld) unexpected", #a, _eb);           \
            cv_test_current_case_fail(__FILE__, __LINE__, _ebuf);     \
        }                                                             \
    } while (0)

#define CV_ASSERT_NULL(ptr)                                           \
    CV_ASSERT((ptr) == NULL)

#define CV_ASSERT_NOT_NULL(ptr)                                       \
    CV_ASSERT((ptr) != NULL)

#endif /* CV_MACROS_H */

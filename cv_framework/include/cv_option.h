/*
 * cv_option.h - Command line option parsing
 */
#ifndef CV_OPTION_H
#define CV_OPTION_H

#define CV_MAX_FILTERS 32

typedef struct cv_test_opts {
    /* --- Test selection --- */
    char    *groups[CV_MAX_FILTERS];
    int      group_count;
    char    *modules[CV_MAX_FILTERS];
    int      module_count;
    char    *cases[CV_MAX_FILTERS];
    int      case_count;
    char    *filter;

    /* --- Execution control --- */
    int      repeat_count;   /* 0 = infinite (continuous mode) */
    int      fail_fast;
    int      timeout_sec;
    int      shuffle;

    /* --- Output control --- */
    int      verbose;
    int      silent;
    int      list_only;
    int      list_detail;
    int      color;           /* -1 = auto-detect */
} cv_test_opts_t;

int  cv_option_parse(int argc, char *argv[], cv_test_opts_t *opts);
void cv_option_print_usage(const char *prog);
void cv_option_free(cv_test_opts_t *opts);

#endif /* CV_OPTION_H */

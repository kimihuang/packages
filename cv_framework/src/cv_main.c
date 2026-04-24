/*
 * cv_main.c - CV Test Framework entry point
 */
#include "cv_test.h"
#include "cv_option.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    cv_test_opts_t opts;

    /* 1. Parse command line */
    int parse_ret = cv_option_parse(argc, argv, &opts);
    if (parse_ret == -2) return 0;   /* --help */
    if (parse_ret != 0)  return 1;   /* parse error */

    /* 2. List-only mode */
    if (opts.list_only || opts.list_detail) {
        cv_list_tests(opts.list_detail);
        cv_option_free(&opts);
        return 0;
    }

    /* 3. Apply filters */
    cv_apply_filters(&opts);

    /* 4. Run tests */
    int ret = cv_run(&opts);

    /* 6. Cleanup */
    cv_option_free(&opts);
    return ret;
}

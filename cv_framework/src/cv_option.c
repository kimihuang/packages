/*
 * cv_option.c - Command line argument parsing
 */
#include "cv_option.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static char *cv_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

static void split_csv(const char *csv, char **out, int *count)
{
    if (!csv || *count >= CV_MAX_FILTERS) return;

    while (*csv && *count < CV_MAX_FILTERS) {
        /* skip leading commas */
        while (*csv == ',') csv++;
        if (!*csv) break;

        const char *start = csv;
        while (*csv && *csv != ',') csv++;

        size_t len = (size_t)(csv - start);
        char *tok = malloc(len + 1);
        if (!tok) return;
        memcpy(tok, start, len);
        tok[len] = '\0';

        out[*count] = tok;
        (*count)++;
    }
}

void cv_option_print_usage(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Test selection:\n");
    printf("  -g, --group <name[,...]>       Run only specified group(s)\n");
    printf("  -m, --module <name[,...]>      Run only specified module(s)\n");
    printf("  -c, --case <name[,...]>        Run only specified case(s)\n");
    printf("  -k, --filter <pattern>         Filter cases by wildcard pattern\n");
    printf("\nExecution control:\n");
    printf("  -n, --count <num>              Repeat execution N times\n");
    printf("  -r, --repeat                   Repeat until failure\n");
    printf("  -f, --fail-fast                Stop on first failure\n");
    printf("      --timeout <sec>            Per-case timeout in seconds\n");
    printf("      --shuffle                  Randomize execution order\n");
    printf("\nOutput control:\n");
    printf("  -v, --verbose                  Verbose output\n");
    printf("  -s, --silent                   Silent mode (summary only)\n");
    printf("      --list                     List all registered tests\n");
    printf("      --list-detail              List tests with details\n");
    printf("      --color                    Colored output\n");
    printf("      --no-color                 Disable colored output\n");
    printf("\nOther:\n");
    printf("  -h, --help                     Show this help\n");
}

static struct option long_options[] = {
    {"group",       required_argument, 0, 'g'},
    {"module",      required_argument, 0, 'm'},
    {"case",        required_argument, 0, 'c'},
    {"filter",      required_argument, 0, 'k'},
    {"count",       required_argument, 0, 'n'},
    {"repeat",      no_argument,       0, 'r'},
    {"fail-fast",   no_argument,       0, 'f'},
    {"timeout",     required_argument, 0, 'T'},
    {"shuffle",     no_argument,       0, 'S'},
    {"verbose",     no_argument,       0, 'v'},
    {"silent",      no_argument,       0, 's'},
    {"list",        no_argument,       0, 'L'},
    {"list-detail", no_argument,       0, 'D'},
    {"color",       no_argument,       0, 'C'},
    {"no-color",    no_argument,       0, 'N'},
    {"help",        no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

int cv_option_parse(int argc, char *argv[], cv_test_opts_t *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->color = -1;  /* auto-detect */
    opts->repeat_count = 1;

    int opt;
    int opt_idx = 0;

    while ((opt = getopt_long(argc, argv, "g:m:c:k:n:rfvsS",
                              long_options, &opt_idx)) != -1) {
        switch (opt) {
        case 'g':
            split_csv(optarg, opts->groups, &opts->group_count);
            break;
        case 'm':
            split_csv(optarg, opts->modules, &opts->module_count);
            break;
        case 'c':
            split_csv(optarg, opts->cases, &opts->case_count);
            break;
        case 'k':
            opts->filter = cv_strdup(optarg);
            break;
        case 'n':
            opts->repeat_count = atoi(optarg);
            if (opts->repeat_count < 1) opts->repeat_count = 1;
            break;
        case 'r':
            opts->repeat_count = 0;  /* infinite */
            break;
        case 'f':
            opts->fail_fast = 1;
            break;
        case 'T':
            opts->timeout_sec = atoi(optarg);
            break;
        case 'S':
            opts->shuffle = 1;
            break;
        case 'v':
            opts->verbose = 1;
            break;
        case 's':
            opts->silent = 1;
            break;
        case 'L':
            opts->list_only = 1;
            break;
        case 'D':
            opts->list_detail = 1;
            break;
        case 'C':
            opts->color = 1;
            break;
        case 'N':
            opts->color = 0;
            break;
        case 'h':
            cv_option_print_usage(argv[0]);
            return -2;  /* special: usage printed, exit 0 */
        default:
            fprintf(stderr, "Error: unknown option '-%c'\n", optopt);
            cv_option_print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

void cv_option_free(cv_test_opts_t *opts)
{
    for (int i = 0; i < opts->group_count; i++)  free(opts->groups[i]);
    for (int i = 0; i < opts->module_count; i++) free(opts->modules[i]);
    for (int i = 0; i < opts->case_count; i++)   free(opts->cases[i]);
    free(opts->filter);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>

#include "sort/sort_registry.h"

#define DEFAULT_SIZE 20

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -a <algorithm>  Sorting algorithm to use\n");
    fprintf(stderr, "  -n <size>       Array size (default: %d)\n", DEFAULT_SIZE);
    fprintf(stderr, "  -s <seed>       Random seed (default: time-based)\n");
    fprintf(stderr, "  -r <range>      Value range 0..RANGE (default: 999)\n");
    fprintf(stderr, "  -p              Print array before and after sorting\n");
    fprintf(stderr, "  -l              List all available algorithms\n");
    fprintf(stderr, "  -b              Run all algorithms benchmark\n");
    fprintf(stderr, "  -h              Show this help\n");
}

static int is_sorted(const int *arr, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (arr[i - 1] > arr[i])
            return 0;
    return 1;
}

static void print_array(const int *arr, size_t n) {
    for (size_t i = 0; i < n; i++)
        printf("%d ", arr[i]);
    printf("\n");
}

static void run_sort(const sort_algorithm_t *algo, int *arr, size_t n, int print) {
    int *copy = NULL;
    if (print) {
        copy = (int *)malloc(n * sizeof(int));
        memcpy(copy, arr, n * sizeof(int));
    }

    clock_t start = clock();
    algo->func(arr, n);
    clock_t end = clock();

    double elapsed = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;

    if (!is_sorted(arr, n)) {
        fprintf(stderr, "  [FAIL] %s produced incorrect result!\n", algo->name);
    } else {
        printf("  %-12s  %8.3f ms  [PASS]\n", algo->name, elapsed);
    }

    if (print && copy) {
        printf("  Before: ");
        print_array(copy, n);
        printf("  After:  ");
        print_array(arr, n);
        free(copy);
    }
}

int main(int argc, char *argv[]) {
    const char *algo_name = NULL;
    size_t size = DEFAULT_SIZE;
    unsigned int seed = 0;
    int range = 999;
    int print_flag = 0;
    int list_flag = 0;
    int bench_flag = 0;
    int seed_set = 0;

    int opt;
    while ((opt = getopt(argc, argv, "a:n:s:r:plbh")) != -1) {
        switch (opt) {
        case 'a':
            algo_name = optarg;
            break;
        case 'n':
            size = (size_t)atoi(optarg);
            break;
        case 's':
            seed = (unsigned int)atoi(optarg);
            seed_set = 1;
            break;
        case 'r':
            range = atoi(optarg);
            break;
        case 'p':
            print_flag = 1;
            break;
        case 'l':
            list_flag = 1;
            break;
        case 'b':
            bench_flag = 1;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    if (!seed_set)
        seed = (unsigned int)time(NULL);

    if (list_flag) {
        printf("Available algorithms:\n");
        const sort_algorithm_t *all = sort_registry_get_all();
        size_t count = sort_registry_count();
        for (size_t i = 0; i < count; i++)
            printf("  %-12s  %s\n", all[i].name, all[i].desc);
        return 0;
    }

    if (!algo_name && !bench_flag) {
        fprintf(stderr, "Error: specify an algorithm with -a or use -b for benchmark\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (algo_name && !sort_registry_find(algo_name)) {
        fprintf(stderr, "Error: unknown algorithm '%s'\n", algo_name);
        return 1;
    }

    /* 生成随机数组 */
    int *arr = (int *)malloc(size * sizeof(int));
    if (!arr) {
        fprintf(stderr, "Error: failed to allocate memory\n");
        return 1;
    }
    srand(seed);
    for (size_t i = 0; i < size; i++)
        arr[i] = rand() % (range + 1);

    printf("Array size: %zu, seed: %u\n", size, seed);

    if (bench_flag) {
        printf("\nBenchmark results:\n");
        const sort_algorithm_t *all = sort_registry_get_all();
        size_t count = sort_registry_count();
        for (size_t i = 0; i < count; i++) {
            int *copy = (int *)malloc(size * sizeof(int));
            if (!copy)
                break;
            memcpy(copy, arr, size * sizeof(int));
            run_sort(&all[i], copy, size, print_flag && size <= 50);
            free(copy);
        }
    } else {
        const sort_algorithm_t *algo = sort_registry_find(algo_name);
        run_sort(algo, arr, size, print_flag);
    }

    free(arr);
    return 0;
}

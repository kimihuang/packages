#include <string.h>
#include "sort/sort_registry.h"
#include "sort/sort_bubble.h"
#include "sort/sort_selection.h"
#include "sort/sort_insertion.h"
#include "sort/sort_shell.h"
#include "sort/sort_merge.h"
#include "sort/sort_quick.h"
#include "sort/sort_heap.h"

/* 所有已注册的排序算法，新增算法只需在此数组中追加一项 */
static const sort_algorithm_t g_algorithms[] = {
    {"bubble",    "Bubble Sort    - O(n^2) stable",    sort_bubble},
    {"selection", "Selection Sort - O(n^2) unstable",  sort_selection},
    {"insertion", "Insertion Sort - O(n^2) stable",    sort_insertion},
    {"shell",     "Shell Sort     - O(n^1.3) unstable", sort_shell},
    {"merge",     "Merge Sort     - O(n log n) stable", sort_merge},
    {"quick",     "Quick Sort     - O(n log n) avg",    sort_quick},
    {"heap",      "Heap Sort      - O(n log n) unstable", sort_heap},
};

#define ALGORITHM_COUNT (sizeof(g_algorithms) / sizeof(g_algorithms[0]))

const sort_algorithm_t *sort_registry_get_all(void) {
    return g_algorithms;
}

size_t sort_registry_count(void) {
    return ALGORITHM_COUNT;
}

const sort_algorithm_t *sort_registry_find(const char *name) {
    if (!name)
        return NULL;
    for (size_t i = 0; i < ALGORITHM_COUNT; i++) {
        if (strcmp(g_algorithms[i].name, name) == 0)
            return &g_algorithms[i];
    }
    return NULL;
}

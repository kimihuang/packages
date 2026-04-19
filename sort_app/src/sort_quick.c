#include <sys/types.h>
#include "sort/sort_quick.h"

static void swap(int *a, int *b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/* 三数取中法选择 pivot，将其放到 hi 位置 */
static int median_of_three(int *arr, ssize_t lo, ssize_t hi) {
    ssize_t mid = lo + (hi - lo) / 2;
    if (arr[lo] > arr[mid])
        swap(&arr[lo], &arr[mid]);
    if (arr[lo] > arr[hi])
        swap(&arr[lo], &arr[hi]);
    if (arr[mid] > arr[hi])
        swap(&arr[mid], &arr[hi]);
    /* pivot 在 mid 位置，交换到 hi */
    swap(&arr[mid], &arr[hi]);
    return arr[hi];
}

/* Lomuto partition scheme */
static ssize_t partition(int *arr, ssize_t lo, ssize_t hi) {
    int pivot = median_of_three(arr, lo, hi);
    ssize_t i = lo;
    for (ssize_t j = lo; j < hi; j++) {
        if (arr[j] <= pivot) {
            swap(&arr[i], &arr[j]);
            i++;
        }
    }
    swap(&arr[i], &arr[hi]);
    return i;
}

static void quick_sort_recursive(int *arr, ssize_t lo, ssize_t hi) {
    if (lo >= hi)
        return;

    /* 小数组切换到插入排序 */
    if (hi - lo < 16) {
        for (ssize_t i = lo + 1; i <= hi; i++) {
            int key = arr[i];
            ssize_t j = i - 1;
            while (j >= lo && arr[j] > key) {
                arr[j + 1] = arr[j];
                j--;
            }
            arr[j + 1] = key;
        }
        return;
    }

    ssize_t p = partition(arr, lo, hi);
    quick_sort_recursive(arr, lo, p - 1);
    quick_sort_recursive(arr, p + 1, hi);
}

void sort_quick(int *arr, size_t n) {
    if (!arr || n < 2)
        return;
    quick_sort_recursive(arr, 0, (ssize_t)n - 1);
}

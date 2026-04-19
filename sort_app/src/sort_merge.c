#include <stdlib.h>
#include <string.h>
#include "sort/sort_merge.h"

static void merge(int *arr, int *tmp, size_t left, size_t mid, size_t right) {
    size_t i = left;
    size_t j = mid;
    size_t k = left;

    while (i < mid && j < right) {
        if (arr[i] <= arr[j])
            tmp[k++] = arr[i++];
        else
            tmp[k++] = arr[j++];
    }
    while (i < mid)
        tmp[k++] = arr[i++];
    while (j < right)
        tmp[k++] = arr[j++];

    memcpy(arr + left, tmp + left, (right - left) * sizeof(int));
}

static void merge_sort_recursive(int *arr, int *tmp, size_t left, size_t right) {
    if (right - left < 2)
        return;

    size_t mid = left + (right - left) / 2;
    merge_sort_recursive(arr, tmp, left, mid);
    merge_sort_recursive(arr, tmp, mid, right);
    merge(arr, tmp, left, mid, right);
}

void sort_merge(int *arr, size_t n) {
    if (!arr || n < 2)
        return;

    int *tmp = (int *)malloc(n * sizeof(int));
    if (!tmp)
        return;

    merge_sort_recursive(arr, tmp, 0, n);
    free(tmp);
}

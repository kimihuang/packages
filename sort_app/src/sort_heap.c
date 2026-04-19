#include <sys/types.h>
#include "sort/sort_heap.h"

static void swap(int *a, int *b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_down(int *arr, size_t start, size_t end) {
    size_t root = start;
    while (2 * root + 1 <= end) {
        size_t child = 2 * root + 1;
        /* 选择较大的子节点 */
        if (child + 1 <= end && arr[child] < arr[child + 1])
            child++;
        if (arr[root] >= arr[child])
            return;
        swap(&arr[root], &arr[child]);
        root = child;
    }
}

void sort_heap(int *arr, size_t n) {
    if (!arr || n < 2)
        return;

    /* 建堆：从最后一个非叶子节点开始 */
    for (ssize_t i = (ssize_t)(n / 2) - 1; i >= 0; i--)
        sift_down(arr, (size_t)i, n - 1);

    /* 逐步取出最大值放到末尾 */
    for (size_t end = n - 1; end > 0; end--) {
        swap(&arr[0], &arr[end]);
        sift_down(arr, 0, end - 1);
    }
}

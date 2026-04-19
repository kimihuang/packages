#include <sys/types.h>
#include "sort/sort_shell.h"

void sort_shell(int *arr, size_t n) {
    if (!arr || n < 2)
        return;

    /* 使用 Knuth 增量序列: h = 3*h + 1 */
    size_t gap = 1;
    while (gap < n / 3)
        gap = gap * 3 + 1;

    while (gap >= 1) {
        for (size_t i = gap; i < n; i++) {
            int key = arr[i];
            ssize_t j = (ssize_t)i - (ssize_t)gap;
            while (j >= 0 && arr[j] > key) {
                arr[j + (ssize_t)gap] = arr[j];
                j -= (ssize_t)gap;
            }
            arr[j + (ssize_t)gap] = key;
        }
        gap /= 3;
    }
}

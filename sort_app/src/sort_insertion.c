#include <sys/types.h>
#include "sort/sort_insertion.h"

void sort_insertion(int *arr, size_t n) {
    if (!arr || n < 2)
        return;

    for (size_t i = 1; i < n; i++) {
        int key = arr[i];
        ssize_t j = (ssize_t)i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

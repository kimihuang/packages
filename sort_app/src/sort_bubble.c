#include "sort/sort_bubble.h"

void sort_bubble(int *arr, size_t n) {
    if (!arr || n < 2)
        return;

    for (size_t i = 0; i < n - 1; i++) {
        int swapped = 0;
        for (size_t j = 0; j < n - 1 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
                swapped = 1;
            }
        }
        if (!swapped)
            break;
    }
}
